// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include "../receiver_base.hpp"
#include <atomic>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

#include "siano_proto.h"
#include "ts_framer.hpp"

namespace px4::s1ud {

class S1udDevice;

class S1udReceiver final : public px4::ReceiverBase {
public:
	S1udReceiver(S1udDevice &parent, std::uintptr_t index);
	~S1udReceiver();

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

	S1udDevice &parent_;
	std::uintptr_t index_;

	std::recursive_mutex lock_;
	std::atomic_bool open_{false};
	std::atomic_bool capturing_{false};
	std::atomic_bool locked_{false};
	std::atomic_bool stop_worker_{false};

	std::unique_ptr<std::thread> worker_thread_;
	px_s1ud::TsFramer ts_framer_;

	float last_signal_level_ = 0.0f;
	DWORD last_stat_time_ = 0;
};

} // namespace px4::s1ud
