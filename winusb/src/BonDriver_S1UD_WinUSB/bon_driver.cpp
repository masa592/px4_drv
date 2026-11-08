#include "bon_driver.hpp"
#include <shlwapi.h>
#include <algorithm>
#include <sstream>

#pragma comment(lib, "shlwapi.lib")

extern HMODULE g_hModule;

namespace px_s1ud {

BonDriverS1UD::BonDriverS1UD()
    : device_(std::make_unique<SianoDevice>())
    , tuner_name_(L"PX-S1UD")
    , device_index_(0)
    , discard_null_packets_(false)
    , exclusive_mutex_(nullptr)
    , current_space_(0)
    , current_channel_(0xFFFFFFFF)
    , internal_buffer_(65536 * 4)
{
    InitChannels();
    InitializeConfig(g_hModule);
}

BonDriverS1UD::~BonDriverS1UD()
{
    CloseTuner();
}

void BonDriverS1UD::InitChannels()
{
    channels_.clear();
    // VHF 1-12 is not used in modern digital broadcasting, UHF 13ch - 62ch
    for (uint32_t ch = 13; ch <= 62; ++ch) {
        ChannelInfo info;
        wchar_t buf[32];
        swprintf_s(buf, L"UHF %02dch", ch);
        info.name = buf;
        info.physical_channel = ch;
        channels_.push_back(info);
    }
}

void BonDriverS1UD::InitializeConfig(HMODULE hModule)
{
    wchar_t module_path[MAX_PATH] = {};
    if (GetModuleFileNameW(hModule, module_path, MAX_PATH) == 0) {
        return;
    }

    std::wstring path = module_path;
    size_t last_dot = path.find_last_of(L'.');
    size_t last_slash = path.find_last_of(L"\\/");

    std::wstring base_name;
    if (last_dot != std::wstring::npos && (last_slash == std::wstring::npos || last_dot > last_slash)) {
        base_name = path.substr(0, last_dot);
    } else {
        base_name = path;
    }

    ini_path_ = base_name + L".ini";

    std::wstring file_name = (last_slash != std::wstring::npos) ? path.substr(last_slash + 1) : path;
    LoadSettings(ini_path_, file_name);
}

void BonDriverS1UD::LoadSettings(const std::wstring &ini_path, const std::wstring &module_name)
{
    // Auto detect tuner index from DLL file name suffix: e.g. BonDriver_S1UD_WinUSB_0.dll -> index 0
    int index_from_name = -1;
    size_t last_underscore = module_name.find_last_of(L'_');
    size_t last_dot = module_name.find_last_of(L'.');
    if (last_underscore != std::wstring::npos && last_dot != std::wstring::npos && last_dot > last_underscore + 1) {
        std::wstring num_str = module_name.substr(last_underscore + 1, last_dot - (last_underscore + 1));
        wchar_t *end_ptr = nullptr;
        long val = wcstol(num_str.c_str(), &end_ptr, 10);
        if (end_ptr != num_str.c_str() && *end_ptr == L'\0' && val >= 0) {
            index_from_name = static_cast<int>(val);
        }
    }

    // Read DeviceIndex from INI
    int default_index = (index_from_name >= 0) ? index_from_name : 0;
    device_index_ = GetPrivateProfileIntW(L"SYSTEM", L"DeviceIndex", default_index, ini_path.c_str());

    // Read TunerName
    wchar_t name_buf[128] = {};
    GetPrivateProfileStringW(L"SYSTEM", L"TunerName", L"PX-S1UD", name_buf, 128, ini_path.c_str());
    if (wcslen(name_buf) > 0) {
        tuner_name_ = name_buf;
        if (device_index_ > 0) {
            wchar_t idx_str[16];
            swprintf_s(idx_str, L" #%d", device_index_);
            tuner_name_ += idx_str;
        }
    }

    // Read FirmwarePath
    wchar_t fw_buf[MAX_PATH] = {};
    GetPrivateProfileStringW(L"SYSTEM", L"FirmwarePath", L"", fw_buf, MAX_PATH, ini_path.c_str());
    fw_path_ = fw_buf;

    // Read DiscardNullPackets
    discard_null_packets_ = (GetPrivateProfileIntW(L"SYSTEM", L"DiscardNullPackets", 0, ini_path.c_str()) != 0);
    device_->SetDiscardNullPackets(discard_null_packets_);
}

const BOOL BonDriverS1UD::OpenTuner(void)
{
    std::lock_guard<std::mutex> lock(api_mutex_);
    if (device_->IsOpened()) {
        return TRUE;
    }

    // Inter-process tuner exclusive locking
    std::wstringstream ss;
    ss << L"Global\\PX_S1UD_Tuner_Lock_" << device_index_;
    mutex_name_ = ss.str();

    exclusive_mutex_ = CreateMutexW(nullptr, FALSE, mutex_name_.c_str());
    if (!exclusive_mutex_) {
        return FALSE;
    }

    DWORD wait_res = WaitForSingleObject(exclusive_mutex_, 100);
    if (wait_res != WAIT_OBJECT_0 && wait_res != WAIT_ABANDONED) {
        // Tuner is currently in use by another application
        CloseHandle(exclusive_mutex_);
        exclusive_mutex_ = nullptr;
        return FALSE;
    }

    if (!device_->Open(device_index_, fw_path_)) {
        ReleaseMutex(exclusive_mutex_);
        CloseHandle(exclusive_mutex_);
        exclusive_mutex_ = nullptr;
        return FALSE;
    }

    current_space_ = 0;
    current_channel_ = 0xFFFFFFFF;
    return TRUE;
}

void BonDriverS1UD::CloseTuner(void)
{
    std::lock_guard<std::mutex> lock(api_mutex_);
    if (device_) {
        device_->Close();
    }
    if (exclusive_mutex_) {
        ReleaseMutex(exclusive_mutex_);
        CloseHandle(exclusive_mutex_);
        exclusive_mutex_ = nullptr;
    }
    current_channel_ = 0xFFFFFFFF;
}

const BOOL BonDriverS1UD::SetChannel(const BYTE bCh)
{
    return SetChannel(0, static_cast<DWORD>(bCh));
}

const float BonDriverS1UD::GetSignalLevel(void)
{
    std::lock_guard<std::mutex> lock(api_mutex_);
    if (!device_ || !device_->IsOpened()) {
        return 0.0f;
    }
    return device_->GetSignalLevel();
}

const DWORD BonDriverS1UD::WaitTsStream(const DWORD dwTimeOut)
{
    std::lock_guard<std::mutex> lock(api_mutex_);
    if (!device_ || !device_->IsOpened()) {
        return WAIT_ABANDONED;
    }

    if (device_->GetReadyCount() > 0) {
        return WAIT_OBJECT_0;
    }

    return device_->WaitTsStream(dwTimeOut) ? WAIT_OBJECT_0 : WAIT_TIMEOUT;
}

const DWORD BonDriverS1UD::GetReadyCount(void)
{
    std::lock_guard<std::mutex> lock(api_mutex_);
    if (!device_ || !device_->IsOpened()) {
        return 0;
    }
    return static_cast<DWORD>(device_->GetReadyCount());
}

const BOOL BonDriverS1UD::GetTsStream(BYTE *pDst, DWORD *pdwSize, DWORD *pdwRemain)
{
    std::lock_guard<std::mutex> lock(api_mutex_);
    if (!device_ || !device_->IsOpened() || !pDst || !pdwSize || !pdwRemain) {
        return FALSE;
    }

    size_t remain = 0;
    size_t copied = device_->ReadTsStream(pDst, *pdwSize, &remain);
    *pdwSize = static_cast<DWORD>(copied);
    *pdwRemain = static_cast<DWORD>(remain);
    return TRUE;
}

const BOOL BonDriverS1UD::GetTsStream(BYTE **ppDst, DWORD *pdwSize, DWORD *pdwRemain)
{
    std::lock_guard<std::mutex> lock(api_mutex_);
    if (!device_ || !device_->IsOpened() || !ppDst || !pdwSize || !pdwRemain) {
        return FALSE;
    }

    size_t remain = 0;
    size_t copied = device_->ReadTsStream(internal_buffer_.data(), internal_buffer_.size(), &remain);
    *ppDst = internal_buffer_.data();
    *pdwSize = static_cast<DWORD>(copied);
    *pdwRemain = static_cast<DWORD>(remain);
    return TRUE;
}

void BonDriverS1UD::PurgeTsStream(void)
{
    std::lock_guard<std::mutex> lock(api_mutex_);
    if (device_) {
        device_->PurgeTsStream();
    }
}

void BonDriverS1UD::Release(void)
{
    delete this;
}

LPCTSTR BonDriverS1UD::GetTunerName(void)
{
    return tuner_name_.c_str();
}

const BOOL BonDriverS1UD::IsTunerOpening(void)
{
    std::lock_guard<std::mutex> lock(api_mutex_);
    return (device_ && device_->IsOpened()) ? TRUE : FALSE;
}

LPCTSTR BonDriverS1UD::EnumTuningSpace(const DWORD dwSpace)
{
    if (dwSpace == 0) {
        return L"UHF";
    }
    return nullptr;
}

LPCTSTR BonDriverS1UD::EnumChannelName(const DWORD dwSpace, const DWORD dwChannel)
{
    if (dwSpace == 0 && dwChannel < channels_.size()) {
        return channels_[dwChannel].name.c_str();
    }
    return nullptr;
}

const BOOL BonDriverS1UD::SetChannel(const DWORD dwSpace, const DWORD dwChannel)
{
    std::lock_guard<std::mutex> lock(api_mutex_);
    if (dwSpace != 0 || dwChannel >= channels_.size()) {
        return FALSE;
    }

    if (!device_ || !device_->IsOpened()) {
        return FALSE;
    }

    uint32_t physical_channel = channels_[dwChannel].physical_channel;
    if (device_->SetChannel(physical_channel)) {
        current_space_ = dwSpace;
        current_channel_ = dwChannel;
        return TRUE;
    }
    return FALSE;
}

const DWORD BonDriverS1UD::GetCurSpace(void)
{
    return current_space_;
}

const DWORD BonDriverS1UD::GetCurChannel(void)
{
    return current_channel_;
}

} // namespace px_s1ud
