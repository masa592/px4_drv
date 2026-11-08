// SPDX-License-Identifier: GPL-2.0-only
#include "dt295_receiver.hpp"
#include "dt295_device.hpp"
#include "../msg.h"

extern "C" {
#include "dt295.h"
}

namespace px4::dt295 {

Dt295Receiver::Dt295Receiver(Dt295Device &parent, std::uintptr_t index)
	: parent_(parent), index_(index)
{
	options_ = RECEIVER_WAIT_AFTER_LOCK_TC_T;
}

Dt295Receiver::~Dt295Receiver()
{
	Term();
}

int Dt295Receiver::Init()
{
	std::lock_guard<std::mutex> lock(lock_);
	msg_info("px4::dt295::Dt295Receiver::Init (index=%" PRIuPTR ")\n", index_);
	return 0;
}

void Dt295Receiver::Term()
{
	std::lock_guard<std::mutex> lock(lock_);
	if (open_.load()) {
		Close();
	}
	msg_info("px4::dt295::Dt295Receiver::Term (index=%" PRIuPTR ")\n", index_);
}

int Dt295Receiver::Open()
{
	std::lock_guard<std::mutex> lock(lock_);
	msg_info("px4::dt295::Dt295Receiver::Open\n");
	open_.store(true);
	locked_.store(false);
	return 0;
}

void Dt295Receiver::Close()
{
	std::lock_guard<std::mutex> lock(lock_);
	msg_info("px4::dt295::Dt295Receiver::Close\n");
	SetCapture(false);
	locked_.store(false);
	open_.store(false);
}

int Dt295Receiver::CheckLock(bool &locked)
{
	locked = locked_.load();
	return 0;
}

int Dt295Receiver::SetLnbVoltage(std::int32_t voltage)
{
	(void)voltage;
	return 0;
}

int Dt295Receiver::SetCapture(bool capture)
{
	capturing_.store(capture);
	return 0;
}

int Dt295Receiver::ReadStat(px4::command::StatType type, std::int32_t &value)
{
	switch (type) {
	case px4::command::StatType::CNR:
		value = locked_.load() ? 28000 : 0;
		return 0;
	default:
		return -ENOSYS;
	}
}

int Dt295Receiver::SetFrequency()
{
	::dt295 *device = parent_.GetDt295Handle();
	if (!device) {
		msg_err("px4::dt295::Dt295Receiver::SetFrequency: device handle is null\n");
		return -ENODEV;
	}

	char error_buf[256] = {};
	int rc = 0;

	if (params_.system == px4::SystemType::ISDB_T) {
		// Frequency in kHz -> channel number
		// UHF 13ch: 473143 kHz, step 6000 kHz
		unsigned channel = 0;
		if (params_.freq >= 473143 && params_.freq <= 767143) {
			channel = 13 + (params_.freq - 473143) / 6000;
		} else if (params_.freq >= 93143 && params_.freq <= 767143) {
			// CATV / mid / high
			channel = (params_.freq - 93143) / 6000;
		}

		if (channel < 13 || channel > 62) {
			msg_err("px4::dt295::Dt295Receiver::SetFrequency: unsupported ISDB-T freq %u kHz (ch %u)\n",
				params_.freq, channel);
			return -EINVAL;
		}

		msg_info("px4::dt295::Dt295Receiver::SetFrequency: tuning terrestrial ch %u (freq %u kHz)\n",
			channel, params_.freq);
		rc = dt295_tune_terrestrial(device, channel, error_buf, sizeof(error_buf));
		if (rc != 0) {
			msg_err("px4::dt295::Dt295Receiver::SetFrequency: dt295_tune_terrestrial failed: %s\n", error_buf);
			locked_.store(false);
			return -EIO;
		}
		locked_.store(true);
		return 0;
	} else if (params_.system == px4::SystemType::ISDB_S) {
		// Frequency in kHz -> IF Hz
		uint32_t if_hz = params_.freq * 1000u;
		int tsid = params_.stream_id ? static_cast<int>(params_.stream_id) : -1;
		msg_info("px4::dt295::Dt295Receiver::SetFrequency: tuning satellite IF %u Hz, tsid %d\n", if_hz, tsid);
		rc = dt295_tune_satellite(device, if_hz, tsid, error_buf, sizeof(error_buf));
		if (rc != 0) {
			msg_err("px4::dt295::Dt295Receiver::SetFrequency: dt295_tune_satellite failed: %s\n", error_buf);
			locked_.store(false);
			return -EIO;
		}
		locked_.store(true);
		return 0;
	}

	msg_err("px4::dt295::Dt295Receiver::SetFrequency: unsupported system\n");
	return -EINVAL;
}

int Dt295Receiver::SetStreamId()
{
	::dt295 *device = parent_.GetDt295Handle();
	if (!device || params_.system != px4::SystemType::ISDB_S)
		return 0;

	char error_buf[256] = {};
	int rc = dt295_select_satellite_tsid(device, static_cast<uint16_t>(params_.stream_id), error_buf, sizeof(error_buf));
	if (rc != 0) {
		msg_err("px4::dt295::Dt295Receiver::SetStreamId: dt295_select_satellite_tsid failed: %s\n", error_buf);
		return -EIO;
	}
	return 0;
}

} // namespace px4::dt295
