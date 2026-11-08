// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include "../receiver_base.hpp"
#include <atomic>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

extern "C" {
#include "dt295.h"
#include "dt295_routes.h"
}

namespace px4::dt295 {

class Dt295Device;

class Dt295Receiver final : public px4::ReceiverBase {
public:
	Dt295Receiver(Dt295Device &parent, std::uintptr_t index);
	~Dt295Receiver();

	int Init();
	void Term();

	int Open() override;
	void Close() override;
	int CheckLock(bool &locked) override;
	int SetLnbVoltage(std::int32_t voltage) override;
	int SetCapture(bool capture) override;
	int ReadStat(px4::command::StatType type, std::int32_t &value) override;

protected:
	int SetFrequency() override;
	int SetStreamId() override;

private:
	void StreamWorker();
	int StartCaptureHardware();
	int StopCaptureHardware();

	Dt295Device &parent_;
	std::uintptr_t index_;

	std::recursive_mutex lock_;
	std::atomic_bool open_{false};
	std::atomic_bool capturing_{false};
	std::atomic_bool locked_{false};
	std::atomic_bool stop_worker_{false};

	std::unique_ptr<std::thread> worker_thread_;
	dt295_routes *routes_ = nullptr;
	dt295_route_plan live_plan_{};
	bool hardware_started_ = false;
	bool key_retained_ = false;
	std::int32_t lnb_voltage_ = 0;
};

} // namespace px4::dt295
