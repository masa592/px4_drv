// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include "../device_base.hpp"
#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "siano_proto.h"

namespace px4::s1ud {

class S1udReceiver;

class S1udDevice final : public px4::DeviceBase {
public:
	S1udDevice(const std::wstring &path, const px4::DeviceDefinition &definition,
		std::uintptr_t index, px4::ReceiverManager &manager);
	~S1udDevice() override;

	int Init() override;
	void Term() override;
	void SetAvailability(bool available) override;
	px4::ReceiverBase *GetReceiver(int id) const override;

	bool HasCardReader() const noexcept override { return false; }
	int OpenCard() override { return -ENOSYS; }
	void CloseCard() override {}
	int DetectCard(bool &detected) override { detected = false; return -ENOSYS; }
	int ResetCard() override { return -ENOSYS; }
	int SetCardBaudrate(::it930x_uart_baudrate baudrate) override { (void)baudrate; return -ENOSYS; }
	int IsCardDataReady(bool &ready) override { ready = false; return -ENOSYS; }
	int ReadCardData(std::uint8_t *buf, std::uint8_t &len) override { (void)buf; len = 0; return -ENOSYS; }
	int WriteCardData(const std::uint8_t *buf, std::uint8_t len) override { (void)buf; (void)len; return -ENOSYS; }

	bool SendCommand(const void *cmd, size_t length);
	bool SendCommandAndWaitResponse(const void *cmd, size_t length, uint16_t expected_res_type, void *res_buf, size_t res_buf_size, DWORD timeout_ms = 2000);
	int ReadStreamBulk(uint8_t *data, size_t length, unsigned timeout_ms, size_t *actual);

	std::recursive_mutex &GetIoMutex() noexcept { return io_mutex_; }
	WINUSB_INTERFACE_HANDLE GetWinUsb() const noexcept { return usb_dev_.winusb; }
	uint8_t GetInEp() const noexcept { return in_ep_; }
	uint8_t GetOutEp() const noexcept { return out_ep_; }

private:
	std::wstring FindFirmwarePath() const;
	bool DetectMode(int &current_mode);
	bool LoadFirmware(const std::wstring &fw_path);
	bool SendFirmwareData(const uint8_t *fw_data, size_t fw_size);
	bool InitDevice(int mode);

	std::atomic_bool available_{true};
	bool registered_ = false;
	mutable std::recursive_mutex io_mutex_;

	uint8_t in_ep_ = 0x81;
	uint8_t out_ep_ = 0x02;

	std::unique_ptr<S1udReceiver> receiver_;
};

} // namespace px4::s1ud
