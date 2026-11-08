// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include "../device_base.hpp"
#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

extern "C" {
#include "dt295.h"
#include "dt295_routes.h"
}

namespace px4::dt295 {

class Dt295Receiver;

class Dt295Device final : public px4::DeviceBase {
public:
	Dt295Device(const std::wstring &path, const px4::DeviceDefinition &definition,
		std::uintptr_t index, px4::ReceiverManager &manager);
	~Dt295Device() override;

	int Init() override;
	void Term() override;
	void SetAvailability(bool available) override;
	px4::ReceiverBase *GetReceiver(int id) const override;

	bool HasCardReader() const noexcept override { return true; }
	int OpenCard() override;
	void CloseCard() override;
	int DetectCard(bool &detected) override;
	int ResetCard() override;
	int SetCardBaudrate(::it930x_uart_baudrate baudrate) override;
	int IsCardDataReady(bool &ready) override;
	int ReadCardData(std::uint8_t *buf, std::uint8_t &len) override;
	int WriteCardData(const std::uint8_t *buf, std::uint8_t len) override;

	::dt295 *GetDt295Handle() const noexcept { return dt295_handle_; }
	const dt295_assets *GetAssets() const noexcept { return assets_; }

private:
	static int ControlCallback(void *user, uint8_t request_type, uint8_t request, uint16_t value,
		uint16_t index, uint8_t *data, size_t length, unsigned timeout_ms, size_t *actual);
	static int BulkCallback(void *user, uint8_t endpoint, uint8_t *data, size_t length,
		unsigned timeout_ms, size_t *actual);
	static int InterruptCallback(void *user, uint8_t endpoint, uint8_t *data, size_t length,
		unsigned timeout_ms, size_t *actual);
	static uint64_t NowMsCallback(void *user);
	static void SleepMsCallback(void *user, unsigned ms);

	std::wstring FindFirmwarePath() const;

	std::atomic_bool available_{true};
	bool registered_ = false;
	std::mutex io_mutex_;
	std::mutex card_mutex_;

	dt295_assets *assets_ = nullptr;
	::dt295 *dt295_handle_ = nullptr;
	dt295_io io_{};

	std::unique_ptr<Dt295Receiver> receiver_;
};

} // namespace px4::dt295
