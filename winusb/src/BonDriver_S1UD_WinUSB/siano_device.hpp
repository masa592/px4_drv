#ifndef SIANO_DEVICE_HPP_
#define SIANO_DEVICE_HPP_

#include <windows.h>
#include <winusb.h>
#include <string>
#include <vector>
#include <thread>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include "siano_proto.h"
#include "ts_framer.hpp"

namespace px_s1ud {

struct SianoDeviceInfo {
    std::wstring device_path;
    uint16_t vid;
    uint16_t pid;
    int index;
};

class SianoDevice {
public:
    SianoDevice();
    ~SianoDevice();

    // Enumerate connected PX-S1UD devices
    static std::vector<SianoDeviceInfo> EnumerateDevices();

    // Open tuner with specific index or path
    bool Open(int target_index = 0, const std::wstring &custom_fw_path = L"");
    void Close();

    bool IsOpened() const { return is_opened_; }

    // Channel tuning (ISDB-T physical channel 13 to 62)
    bool SetChannel(uint32_t channel);

    // Signal statistics (returns dB value, e.g. 0.0 - 35.0 dB)
    float GetSignalLevel();

    // Stream retrieval
    size_t GetReadyCount();
    size_t ReadTsStream(uint8_t *dst, size_t max_bytes, size_t *remain_bytes);
    void PurgeTsStream();
    bool WaitTsStream(DWORD timeout_ms);

    void SetDiscardNullPackets(bool discard)
    {
        ts_framer_.SetDiscardNullPackets(discard);
    }

private:
    bool InitializeWinUsb(const std::wstring &device_path);
    void CleanupWinUsb();

    bool DetectMode(int &current_mode);
    bool LoadFirmware(const std::wstring &fw_path);
    bool SendFirmwareData(const uint8_t *fw_data, size_t fw_size);
    bool InitDevice(int mode);

    bool SendCommand(const void *cmd, size_t length);
    bool SendCommandAndWaitResponse(const void *cmd, size_t length, uint16_t expected_res_type, void *res_buf, size_t res_buf_size, DWORD timeout_ms = 2000);

    // Stream reception worker thread
    void StreamWorkerThread();

    HANDLE device_handle_;
    WINUSB_INTERFACE_HANDLE winusb_handle_;
    uint8_t in_ep_;
    uint8_t out_ep_;
    size_t in_ep_max_packet_;

    std::atomic<bool> is_opened_;
    std::atomic<bool> is_streaming_;
    std::thread stream_thread_;

    HANDLE stream_event_;
    TsFramer ts_framer_;

    int current_channel_;
    float last_signal_level_;
    DWORD last_stat_time_;

    std::mutex cmd_mutex_;
    std::wstring current_device_path_;
};

} // namespace px_s1ud

#endif // SIANO_DEVICE_HPP_
