#ifndef BON_DRIVER_HPP_
#define BON_DRIVER_HPP_

#include <windows.h>
#include <string>
#include <vector>
#include <memory>
#include <mutex>
#include "../../include/IBonDriver2.h"
#include "siano_device.hpp"

namespace px_s1ud {

struct ChannelInfo {
    std::wstring name;
    uint32_t physical_channel; // 13 - 62
};

class BonDriverS1UD : public IBonDriver2 {
public:
    BonDriverS1UD();
    virtual ~BonDriverS1UD();

    // IBonDriver
    virtual const BOOL OpenTuner(void) override;
    virtual void CloseTuner(void) override;
    virtual const BOOL SetChannel(const BYTE bCh) override;
    virtual const float GetSignalLevel(void) override;
    virtual const DWORD WaitTsStream(const DWORD dwTimeOut = 0) override;
    virtual const DWORD GetReadyCount(void) override;
    virtual const BOOL GetTsStream(BYTE *pDst, DWORD *pdwSize, DWORD *pdwRemain) override;
    virtual const BOOL GetTsStream(BYTE **ppDst, DWORD *pdwSize, DWORD *pdwRemain) override;
    virtual void PurgeTsStream(void) override;
    virtual void Release(void) override;

    // IBonDriver2
    virtual LPCTSTR GetTunerName(void) override;
    virtual const BOOL IsTunerOpening(void) override;
    virtual LPCTSTR EnumTuningSpace(const DWORD dwSpace) override;
    virtual LPCTSTR EnumChannelName(const DWORD dwSpace, const DWORD dwChannel) override;
    virtual const BOOL SetChannel(const DWORD dwSpace, const DWORD dwChannel) override;
    virtual const DWORD GetCurSpace(void) override;
    virtual const DWORD GetCurChannel(void) override;

    // Module initialization helper
    void InitializeConfig(HMODULE hModule);

private:
    void LoadSettings(const std::wstring &ini_path, const std::wstring &module_name);
    void InitChannels();

    std::unique_ptr<SianoDevice> device_;
    std::wstring tuner_name_;
    std::wstring ini_path_;
    std::wstring fw_path_;
    int device_index_;
    bool discard_null_packets_;

    HANDLE exclusive_mutex_;
    std::wstring mutex_name_;

    std::vector<ChannelInfo> channels_;
    DWORD current_space_;
    DWORD current_channel_;

    std::vector<uint8_t> internal_buffer_;
    std::mutex api_mutex_;
};

} // namespace px_s1ud

#endif // BON_DRIVER_HPP_
