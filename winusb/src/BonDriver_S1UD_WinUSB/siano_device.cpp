#include "siano_device.hpp"
#include <setupapi.h>
#include <initguid.h>
#include <devguid.h>
#include <iostream>
#include <vector>
#include <fstream>
#include <algorithm>

#pragma comment(lib, "setupapi.lib")
#pragma comment(lib, "winusb.lib")

// {7D1A23D8-8A31-4F98-8B42-4A7389146522}
DEFINE_GUID(GUID_DEVINTERFACE_PXS1UD,
    0x7d1a23d8, 0x8a31, 0x4f98, 0x8b, 0x42, 0x4a, 0x73, 0x89, 0x14, 0x65, 0x22);

// Standard WinUSB Device Interface GUID used by Zadig default
DEFINE_GUID(GUID_DEVINTERFACE_WINUSB_GENERIC,
    0xa5dcbf10, 0x6530, 0x11d2, 0x90, 0x1f, 0x00, 0xc0, 0x4f, 0xb9, 0x51, 0xed);

namespace px_s1ud {

SianoDevice::SianoDevice()
    : device_handle_(INVALID_HANDLE_VALUE)
    , winusb_handle_(nullptr)
    , in_ep_(0x81)
    , out_ep_(0x02)
    , in_ep_max_packet_(512)
    , is_opened_(false)
    , is_streaming_(false)
    , stream_event_(CreateEvent(nullptr, TRUE, FALSE, nullptr))
    , current_channel_(-1)
    , last_signal_level_(0.0f)
    , last_stat_time_(0)
{
}

SianoDevice::~SianoDevice()
{
    Close();
    if (stream_event_) {
        CloseHandle(stream_event_);
        stream_event_ = nullptr;
    }
}

std::vector<SianoDeviceInfo> SianoDevice::EnumerateDevices()
{
    std::vector<SianoDeviceInfo> devices;

    // GUIDs to search for: custom PXS1UD GUID, and standard WinUSB GUID
    const GUID* guids[] = { &GUID_DEVINTERFACE_PXS1UD, &GUID_DEVINTERFACE_WINUSB_GENERIC };

    int found_index = 0;
    for (const GUID *guid : guids) {
        HDEVINFO hDevInfo = SetupDiGetClassDevs(guid, nullptr, nullptr, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
        if (hDevInfo == INVALID_HANDLE_VALUE) continue;

        SP_DEVICE_INTERFACE_DATA interfaceData = {};
        interfaceData.cbSize = sizeof(SP_DEVICE_INTERFACE_DATA);

        for (DWORD i = 0; SetupDiEnumDeviceInterfaces(hDevInfo, nullptr, guid, i, &interfaceData); ++i) {
            DWORD requiredSize = 0;
            SetupDiGetDeviceInterfaceDetail(hDevInfo, &interfaceData, nullptr, 0, &requiredSize, nullptr);

            if (requiredSize == 0) continue;

            std::vector<uint8_t> detailBuffer(requiredSize);
            PSP_DEVICE_INTERFACE_DETAIL_DATA detailData = reinterpret_cast<PSP_DEVICE_INTERFACE_DETAIL_DATA>(detailBuffer.data());
            detailData->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA);

            SP_DEVINFO_DATA devInfoData = {};
            devInfoData.cbSize = sizeof(SP_DEVINFO_DATA);

            if (SetupDiGetDeviceInterfaceDetail(hDevInfo, &interfaceData, detailData, requiredSize, nullptr, &devInfoData)) {
                std::wstring path = detailData->DevicePath;
                // Parse VID and PID
                // Example path: \\?\usb#vid_0511&pid_0246#...
                std::wstring lower_path = path;
                std::transform(lower_path.begin(), lower_path.end(), lower_path.begin(), ::towlower);

                uint16_t vid = 0;
                uint16_t pid = 0;
                size_t vid_pos = lower_path.find(L"vid_");
                size_t pid_pos = lower_path.find(L"pid_");

                if (vid_pos != std::wstring::npos && pid_pos != std::wstring::npos) {
                    vid = static_cast<uint16_t>(wcstol(lower_path.c_str() + vid_pos + 4, nullptr, 16));
                    pid = static_cast<uint16_t>(wcstol(lower_path.c_str() + pid_pos + 4, nullptr, 16));
                }

                // Check if matching PX-S1UD V1.0 or V2.0
                if ((vid == 0x0511 && pid == 0x0246) || (vid == 0x3275 && pid == 0x0080) ||
                    (vid == 0x187f && pid == 0x0600) || (vid == 0x187f && pid == 0x0201) ||
                    (vid == 0x187f && pid == 0x0010)) {

                    // Check if already in list
                    bool already_exists = false;
                    for (const auto &d : devices) {
                        if (d.device_path == path) {
                            already_exists = true;
                            break;
                        }
                    }

                    if (!already_exists) {
                        SianoDeviceInfo info;
                        info.device_path = path;
                        info.vid = vid;
                        info.pid = pid;
                        info.index = found_index++;
                        devices.push_back(info);
                    }
                }
            }
        }
        SetupDiDestroyDeviceInfoList(hDevInfo);
    }

    return devices;
}

bool SianoDevice::InitializeWinUsb(const std::wstring &device_path)
{
    device_handle_ = CreateFile(
        device_path.c_str(),
        GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        nullptr,
        OPEN_EXISTING,
        FILE_FLAG_OVERLAPPED,
        nullptr);

    if (device_handle_ == INVALID_HANDLE_VALUE) {
        return false;
    }

    if (!WinUsb_Initialize(device_handle_, &winusb_handle_)) {
        CloseHandle(device_handle_);
        device_handle_ = INVALID_HANDLE_VALUE;
        return false;
    }

    // Discover endpoints
    USB_INTERFACE_DESCRIPTOR ifaceDesc = {};
    if (WinUsb_QueryInterfaceSettings(winusb_handle_, 0, &ifaceDesc)) {
        for (UCHAR i = 0; i < ifaceDesc.bNumEndpoints; ++i) {
            WINUSB_PIPE_INFORMATION pipeInfo = {};
            if (WinUsb_QueryPipe(winusb_handle_, 0, i, &pipeInfo)) {
                if (USB_ENDPOINT_DIRECTION_IN(pipeInfo.PipeId)) {
                    in_ep_ = pipeInfo.PipeId;
                    in_ep_max_packet_ = pipeInfo.MaximumPacketSize;
                } else {
                    out_ep_ = pipeInfo.PipeId;
                }
            }
        }
    }

    // Set pipe policy
    BOOL raw_io = TRUE;
    WinUsb_SetPipePolicy(winusb_handle_, in_ep_, RAW_IO, sizeof(raw_io), &raw_io);
    ULONG timeout = 2000;
    WinUsb_SetPipePolicy(winusb_handle_, in_ep_, PIPE_TRANSFER_TIMEOUT, sizeof(timeout), &timeout);
    WinUsb_SetPipePolicy(winusb_handle_, out_ep_, PIPE_TRANSFER_TIMEOUT, sizeof(timeout), &timeout);

    return true;
}

void SianoDevice::CleanupWinUsb()
{
    if (winusb_handle_) {
        WinUsb_Free(winusb_handle_);
        winusb_handle_ = nullptr;
    }
    if (device_handle_ != INVALID_HANDLE_VALUE) {
        CloseHandle(device_handle_);
        device_handle_ = INVALID_HANDLE_VALUE;
    }
}

bool SianoDevice::Open(int target_index, const std::wstring &custom_fw_path)
{
    if (is_opened_) return true;

    auto devices = EnumerateDevices();
    if (devices.empty() || target_index < 0 || target_index >= static_cast<int>(devices.size())) {
        return false;
    }

    current_device_path_ = devices[target_index].device_path;
    if (!InitializeWinUsb(current_device_path_)) {
        return false;
    }

    // Check device mode and determine if firmware needs to be loaded
    int current_mode = DEVICE_MODE_NONE;
    DetectMode(current_mode);

    if (current_mode != DEVICE_MODE_ISDBT && current_mode != DEVICE_MODE_ISDBT_BDA) {
        if (!LoadFirmware(custom_fw_path)) {
            // If firmware load fails, still try to see if device is already initialized
        }
        InitDevice(DEVICE_MODE_ISDBT_BDA);
    }

    ts_framer_.Reset();
    ResetEvent(stream_event_);

    is_opened_ = true;
    is_streaming_ = true;
    stream_thread_ = std::thread(&SianoDevice::StreamWorkerThread, this);

    return true;
}

void SianoDevice::Close()
{
    if (!is_opened_) return;

    is_streaming_ = false;
    if (stream_thread_.joinable()) {
        if (winusb_handle_) {
            WinUsb_AbortPipe(winusb_handle_, in_ep_);
        }
        stream_thread_.join();
    }

    is_opened_ = false;
    CleanupWinUsb();
    ts_framer_.Reset();
}

bool SianoDevice::DetectMode(int &current_mode)
{
    current_mode = DEVICE_MODE_NONE;
    sms_msg_hdr req = {};
    InitSianoMsgHdr(&req, MSG_SMS_GET_VERSION_EX_REQ, 0, HIF_TASK, sizeof(req));

    sms_version_res res = {};
    if (SendCommandAndWaitResponse(&req, sizeof(req), MSG_SMS_GET_VERSION_EX_RES, &res, sizeof(res), 1500)) {
        current_mode = res.firmware_id == 255 ? DEVICE_MODE_NONE : res.firmware_id;
        return true;
    }
    return false;
}

bool SianoDevice::LoadFirmware(const std::wstring &custom_fw_path)
{
    // Possible firmware filenames
    std::vector<std::wstring> candidates;
    if (!custom_fw_path.empty()) {
        candidates.push_back(custom_fw_path);
    }
    candidates.push_back(L"isdbt_rio.inp");
    candidates.push_back(L"isdbt_nova_12mhz_b0.inp");
    candidates.push_back(L"sms1xxx-hcw-55xxx-isdbt-02.fw");

    // Also check same directory as module DLL
    wchar_t module_path[MAX_PATH] = {};
    HMODULE hModule = nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       reinterpret_cast<LPCWSTR>(&GUID_DEVINTERFACE_PXS1UD), &hModule);
    if (hModule) {
        GetModuleFileNameW(hModule, module_path, MAX_PATH);
        wchar_t *last_slash = wcsrchr(module_path, L'\\');
        if (last_slash) {
            *(last_slash + 1) = L'\0';
            candidates.push_back(std::wstring(module_path) + L"isdbt_rio.inp");
            candidates.push_back(std::wstring(module_path) + L"isdbt_nova_12mhz_b0.inp");
        }
    }

    std::vector<uint8_t> fw_data;
    for (const auto &path : candidates) {
        std::ifstream file(path, std::ios::binary | std::ios::ate);
        if (file.is_open()) {
            std::streamsize size = file.tellg();
            file.seekg(0, std::ios::beg);
            fw_data.resize(static_cast<size_t>(size));
            if (file.read(reinterpret_cast<char*>(fw_data.data()), size)) {
                break;
            }
            fw_data.clear();
        }
    }

    if (fw_data.empty()) {
        return false;
    }

    return SendFirmwareData(fw_data.data(), fw_data.size());
}

bool SianoDevice::SendFirmwareData(const uint8_t *fw_data, size_t fw_size)
{
    if (!fw_data || fw_size < sizeof(sms_firmware_header)) return false;

    const sms_firmware_header *fw_hdr = reinterpret_cast<const sms_firmware_header*>(fw_data);
    uint32_t start_addr = fw_hdr->start_address;
    uint32_t length = fw_hdr->length;
    const uint8_t *payload = fw_data + sizeof(sms_firmware_header);
    size_t payload_len = fw_size - sizeof(sms_firmware_header);

    // Calculate checksum
    uint32_t checksum = 0;
    const uint32_t *u32_ptr = reinterpret_cast<const uint32_t*>(payload);
    for (size_t i = 0; i < length / 4; ++i) {
        checksum += u32_ptr[i];
    }

    // 1. Reload start
    sms_msg_hdr reload_req = {};
    InitSianoMsgHdr(&reload_req, MSG_SW_RELOAD_START_REQ, 0, HIF_TASK, sizeof(reload_req));
    sms_msg_hdr reload_res = {};
    SendCommandAndWaitResponse(&reload_req, sizeof(reload_req), MSG_SW_RELOAD_START_RES, &reload_res, sizeof(reload_res), 1000);

    // 2. Download chunks
    uint32_t current_addr = start_addr;
    size_t remain = payload_len;
    const uint8_t *cur_payload = payload;

    while (remain > 0) {
        size_t chunk_size = (std::min)(remain, static_cast<size_t>(SMS_MAX_PAYLOAD_SIZE));
        sms_data_download dl_msg = {};
        InitSianoMsgHdr(&dl_msg.x_msg_header, MSG_SMS_DATA_DOWNLOAD_REQ, 0, HIF_TASK,
                        static_cast<uint16_t>(sizeof(sms_msg_hdr) + sizeof(uint32_t) + chunk_size));
        dl_msg.mem_addr = current_addr;
        std::memcpy(dl_msg.payload, cur_payload, chunk_size);

        sms_msg_hdr dl_res = {};
        if (!SendCommandAndWaitResponse(&dl_msg, dl_msg.x_msg_header.msg_length, MSG_SMS_DATA_DOWNLOAD_RES, &dl_res, sizeof(dl_res), 1000)) {
            return false;
        }

        cur_payload += chunk_size;
        current_addr += static_cast<uint32_t>(chunk_size);
        remain -= chunk_size;
    }

    // 3. Validity check
    struct {
        sms_msg_hdr hdr;
        uint32_t data[3];
    } val_msg = {};
    InitSianoMsgHdr(&val_msg.hdr, MSG_SMS_DATA_VALIDITY_REQ, 0, HIF_TASK, sizeof(val_msg));
    val_msg.data[0] = start_addr;
    val_msg.data[1] = length;
    val_msg.data[2] = 0;

    sms_msg_data val_res = {};
    if (!SendCommandAndWaitResponse(&val_msg, sizeof(val_msg), MSG_SMS_DATA_VALIDITY_RES, &val_res, sizeof(val_res), 1500)) {
        return false;
    }

    // 4. Trigger / Reload exec
    sms_msg_data5 trig_msg = {};
    InitSianoMsgHdr(&trig_msg.x_msg_header, MSG_SMS_SWDOWNLOAD_TRIGGER_REQ, 0, HIF_TASK, sizeof(trig_msg));
    trig_msg.msg_data[0] = start_addr;
    trig_msg.msg_data[1] = 6;     // Priority
    trig_msg.msg_data[2] = 0x200; // Stack size
    trig_msg.msg_data[3] = 0;     // Parameter
    trig_msg.msg_data[4] = 4;     // Task ID

    sms_msg_hdr trig_res = {};
    SendCommandAndWaitResponse(&trig_msg, sizeof(trig_msg), MSG_SMS_SWDOWNLOAD_TRIGGER_RES, &trig_res, sizeof(trig_res), 1000);

    Sleep(400);
    return true;
}

bool SianoDevice::InitDevice(int mode)
{
    sms_msg_data msg = {};
    InitSianoMsgHdr(&msg.x_msg_header, MSG_SMS_INIT_DEVICE_REQ, DVBT_BDA_CONTROL_MSG_ID, HIF_TASK, sizeof(msg));
    msg.msg_data = mode;

    sms_msg_hdr res = {};
    return SendCommandAndWaitResponse(&msg, sizeof(msg), MSG_SMS_INIT_DEVICE_RES, &res, sizeof(res), 1500);
}

bool SianoDevice::SendCommand(const void *cmd, size_t length)
{
    if (!winusb_handle_) return false;
    ULONG written = 0;
    return WinUsb_WritePipe(winusb_handle_, out_ep_, const_cast<PUCHAR>(static_cast<const uint8_t*>(cmd)), static_cast<ULONG>(length), &written, nullptr) == TRUE;
}

bool SianoDevice::SendCommandAndWaitResponse(const void *cmd, size_t length, uint16_t expected_res_type, void *res_buf, size_t res_buf_size, DWORD timeout_ms)
{
    std::lock_guard<std::mutex> lock(cmd_mutex_);
    if (!SendCommand(cmd, length)) {
        return false;
    }

    DWORD start_time = GetTickCount();
    while (GetTickCount() - start_time < timeout_ms) {
        uint8_t buffer[2048] = {};
        ULONG read_bytes = 0;
        if (WinUsb_ReadPipe(winusb_handle_, in_ep_, buffer, sizeof(buffer), &read_bytes, nullptr) && read_bytes >= sizeof(sms_msg_hdr)) {
            const sms_msg_hdr *hdr = reinterpret_cast<const sms_msg_hdr*>(buffer);
            if (hdr->msg_type == expected_res_type) {
                if (res_buf && res_buf_size > 0) {
                    size_t copy_size = (std::min)(static_cast<size_t>(read_bytes), res_buf_size);
                    std::memcpy(res_buf, buffer, copy_size);
                }
                return true;
            }
        }
        Sleep(10);
    }
    return false;
}

bool SianoDevice::SetChannel(uint32_t channel)
{
    if (!is_opened_) return false;

    // ISDB-T Physical 13ch -> 473.142857 MHz (center frequency 473142857 Hz)
    // Channel separation: 6 MHz
    if (channel < 13 || channel > 62) {
        return false;
    }

    uint32_t frequency_hz = 473142857 + (channel - 13) * 6000000;

    struct {
        sms_msg_hdr hdr;
        uint32_t data[4];
    } tune_msg = {};

    InitSianoMsgHdr(&tune_msg.hdr, MSG_SMS_ISDBT_TUNE_REQ, DVBT_BDA_CONTROL_MSG_ID, HIF_TASK, sizeof(tune_msg));
    tune_msg.data[0] = frequency_hz;
    tune_msg.data[1] = BW_ISDBT_13SEG; // 13-segment Fullseg/1seg reception
    tune_msg.data[2] = 12000000;       // Crystal
    tune_msg.data[3] = 0;              // Segment index

    sms_msg_hdr tune_res = {};
    bool ok = SendCommandAndWaitResponse(&tune_msg, sizeof(tune_msg), MSG_SMS_ISDBT_TUNE_RES, &tune_res, sizeof(tune_res), 2000);
    if (ok) {
        current_channel_ = channel;
        PurgeTsStream();
    }
    return ok;
}

float SianoDevice::GetSignalLevel()
{
    if (!is_opened_) return 0.0f;

    DWORD now = GetTickCount();
    if (now - last_stat_time_ < 500) {
        return last_signal_level_;
    }
    last_stat_time_ = now;

    sms_msg_hdr stat_req = {};
    InitSianoMsgHdr(&stat_req, MSG_SMS_GET_STATISTICS_EX_REQ, DVBT_BDA_CONTROL_MSG_ID, HIF_TASK, sizeof(stat_req));

    sms_isdbt_stats_ex stat_res = {};
    if (SendCommandAndWaitResponse(&stat_req, sizeof(stat_req), MSG_SMS_GET_STATISTICS_EX_RES, &stat_res, sizeof(stat_res), 500)) {
        if (stat_res.is_demod_locked || stat_res.is_rf_locked) {
            // stat_res.snr is in dB (e.g. 24 dB)
            last_signal_level_ = static_cast<float>(stat_res.snr);
            if (last_signal_level_ < 0.0f) last_signal_level_ = 0.0f;
        } else {
            last_signal_level_ = 0.0f;
        }
    }
    return last_signal_level_;
}

size_t SianoDevice::GetReadyCount()
{
    return ts_framer_.GetReadyCount();
}

size_t SianoDevice::ReadTsStream(uint8_t *dst, size_t max_bytes, size_t *remain_bytes)
{
    size_t read_bytes = ts_framer_.PopTsPackets(dst, max_bytes, remain_bytes);
    if (ts_framer_.GetReadyCount() == 0) {
        ResetEvent(stream_event_);
    }
    return read_bytes;
}

void SianoDevice::PurgeTsStream()
{
    ts_framer_.Reset();
    ResetEvent(stream_event_);
}

bool SianoDevice::WaitTsStream(DWORD timeout_ms)
{
    if (ts_framer_.GetReadyCount() > 0) {
        return true;
    }
    DWORD res = WaitForSingleObject(stream_event_, timeout_ms);
    return (res == WAIT_OBJECT_0);
}

void SianoDevice::StreamWorkerThread()
{
    constexpr size_t BUFFER_SIZE = 65536;
    std::vector<uint8_t> rx_buffer(BUFFER_SIZE);

    while (is_streaming_) {
        ULONG bytes_read = 0;
        BOOL success = WinUsb_ReadPipe(winusb_handle_, in_ep_, rx_buffer.data(), BUFFER_SIZE, &bytes_read, nullptr);

        if (success && bytes_read > 0) {
            // Siano data messages contain sms_msg_hdr
            if (bytes_read >= sizeof(sms_msg_hdr)) {
                const sms_msg_hdr *hdr = reinterpret_cast<const sms_msg_hdr*>(rx_buffer.data());
                if (hdr->msg_type == MSG_SMS_DVBT_BDA_DATA || hdr->msg_type == MSG_SMS_DATA_MSG) {
                    // TS payload follows the header
                    size_t header_len = sizeof(sms_msg_hdr);
                    if (bytes_read > header_len) {
                        ts_framer_.FeedRawData(rx_buffer.data() + header_len, bytes_read - header_len);
                        SetEvent(stream_event_);
                    }
                    continue;
                }
            }

            // Also check if stream directly contains TS packets
            ts_framer_.FeedRawData(rx_buffer.data(), bytes_read);
            if (ts_framer_.GetReadyCount() > 0) {
                SetEvent(stream_event_);
            }
        } else {
            DWORD err = GetLastError();
            if (err == ERROR_SEM_TIMEOUT || err == ERROR_OPERATION_ABORTED) {
                // Normal timeout or abort during close
            } else {
                Sleep(10);
            }
        }
    }
}

} // namespace px_s1ud
