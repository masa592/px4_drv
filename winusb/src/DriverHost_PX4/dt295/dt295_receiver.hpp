// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include "../receiver_base.hpp"
#include <atomic>
#include <memory>
#include <mutex>

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
	Dt295Device &parent_;
	std::uintptr_t index_;

	std::mutex lock_;
	std::atomic_bool open_{false};
	std::atomic_bool capturing_{false};
	std::atomic_bool locked_{false};
};

} // namespace px4::dt295
