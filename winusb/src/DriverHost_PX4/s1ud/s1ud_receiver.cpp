// SPDX-License-Identifier: GPL-2.0-only
#define _CRT_SECURE_NO_WARNINGS
#include "s1ud_receiver.hpp"
#include "s1ud_device.hpp"
#include "msg.h"

#include <algorithm>
#include <chrono>
#include <cinttypes>
#include <cstring>

namespace px4::s1ud {

S1udReceiver::S1udReceiver(S1udDevice &parent, std::uintptr_t index)
	: ReceiverBase(RECEIVER_WAIT_AFTER_LOCK_TC_T),
	parent_(parent),
	index_(index)
{
}

S1udReceiver::~S1udReceiver()
{
	Term();
}

int S1udReceiver::Init()
{
	std::lock_guard<std::recursive_mutex> lock(lock_);
	msg_info("px4::s1ud::S1udReceiver::Init (index=%" PRIuPTR ")\n", index_);

	std::size_t stream_size = 188 * 4096;
	stream_buf_->Alloc(stream_size);
	stream_buf_->SetThresholdSize(stream_size / 10);

	return 0;
}

void S1udReceiver::Term()
{
	std::lock_guard<std::recursive_mutex> lock(lock_);
	if (open_.load()) {
		Close();
	}

	msg_info("px4::s1ud::S1udReceiver::Term (index=%" PRIuPTR ")\n", index_);
}

int S1udReceiver::Open()
{
	std::lock_guard<std::recursive_mutex> lock(lock_);
	msg_info("px4::s1ud::S1udReceiver::Open\n");
	open_.store(true);
	locked_.store(false);
	return 0;
}

void S1udReceiver::Close()
{
	std::lock_guard<std::recursive_mutex> lock(lock_);
	msg_info("px4::s1ud::S1udReceiver::Close\n");
	SetCapture(false);
	locked_.store(false);
	open_.store(false);
}

int S1udReceiver::CheckLock(bool &locked)
{
	locked = locked_.load();
	return 0;
}

int S1udReceiver::SetLnbVoltage(std::int32_t voltage)
{
	(void)voltage;
	return 0;
}

int S1udReceiver::SetCapture(bool capture)
{
	std::lock_guard<std::recursive_mutex> lock(lock_);

	if (capturing_.load() == capture)
		return 0;

	if (capture) {
		msg_info("px4::s1ud::S1udReceiver::SetCapture(true)\n");
		ts_framer_.Reset();
		stream_buf_->Start();
		stream_buf_->Purge();

		stop_worker_.store(false);
		capturing_.store(true);
		worker_thread_ = std::make_unique<std::thread>(&S1udReceiver::StreamWorker, this);
	} else {
		msg_info("px4::s1ud::S1udReceiver::SetCapture(false)\n");
		capturing_.store(false);
		stop_worker_.store(true);

		if (worker_thread_ && worker_thread_->joinable()) {
			worker_thread_->join();
			worker_thread_.reset();
		}

		stream_buf_->Stop();
		ts_framer_.Reset();
	}

	return 0;
}

int S1udReceiver::ReadStat(px4::command::StatType type, std::int32_t &value)
{
	switch (type) {
	case px4::command::StatType::CNR:
	{
		auto now = std::chrono::steady_clock::now();
		if (std::chrono::duration_cast<std::chrono::milliseconds>(now - last_stat_time_).count() < 500) {
			value = static_cast<std::int32_t>(last_signal_level_ * 1000.0f);
			return 0;
		}
		last_stat_time_ = now;

		sms_msg_hdr stat_req = {};
		InitSianoMsgHdr(&stat_req, MSG_SMS_GET_STATISTICS_EX_REQ, DVBT_BDA_CONTROL_MSG_ID, HIF_TASK, sizeof(stat_req));

		sms_isdbt_stats_ex stat_res = {};
		if (parent_.SendCommandAndWaitResponse(&stat_req, sizeof(stat_req), MSG_SMS_GET_STATISTICS_EX_RES, &stat_res, sizeof(stat_res), 500)) {
			if (stat_res.is_demod_locked || stat_res.is_rf_locked) {
				last_signal_level_ = static_cast<float>(stat_res.snr);
				if (last_signal_level_ < 0.0f) last_signal_level_ = 0.0f;
				locked_.store(true);
			} else {
				last_signal_level_ = 0.0f;
			}
		}
		value = static_cast<std::int32_t>(last_signal_level_ * 1000.0f);
		return 0;
	}
	default:
		return -ENOSYS;
	}
}

int S1udReceiver::SetFrequency()
{
	if (params_.system != px4::SystemType::ISDB_T) {
		msg_err("px4::s1ud::S1udReceiver::SetFrequency: unsupported system\n");
		return -EINVAL;
	}

	unsigned channel = 0;
	if (params_.freq >= 473143 && params_.freq <= 767143) {
		channel = 13 + (params_.freq - 473143) / 6000;
	} else if (params_.freq >= 93143 && params_.freq <= 767143) {
		channel = (params_.freq - 93143) / 6000;
	}

	if (channel < 13 || channel > 62) {
		msg_err("px4::s1ud::S1udReceiver::SetFrequency: unsupported ISDB-T freq %u kHz (ch %u)\n",
			params_.freq, channel);
		return -EINVAL;
	}

	uint32_t frequency_hz = 473142857 + (channel - 13) * 6000000;
	msg_info("px4::s1ud::S1udReceiver::SetFrequency: tuning terrestrial ch %u (freq %u Hz)\n",
		channel, frequency_hz);

	struct {
		sms_msg_hdr hdr;
		uint32_t data[4];
	} tune_msg = {};

	InitSianoMsgHdr(&tune_msg.hdr, MSG_SMS_ISDBT_TUNE_REQ, DVBT_BDA_CONTROL_MSG_ID, HIF_TASK, sizeof(tune_msg));
	tune_msg.data[0] = frequency_hz;
	tune_msg.data[1] = BW_ISDBT_13SEG; // 13-segment
	tune_msg.data[2] = 12000000;       // Crystal
	tune_msg.data[3] = 0;              // Segment index

	sms_msg_hdr tune_res = {};
	bool ok = parent_.SendCommandAndWaitResponse(&tune_msg, sizeof(tune_msg), MSG_SMS_ISDBT_TUNE_RES, &tune_res, sizeof(tune_res), 2000);
	if (ok) {
		locked_.store(true);
		ts_framer_.Reset();
		return 0;
	}

	msg_err("px4::s1ud::S1udReceiver::SetFrequency: MSG_SMS_ISDBT_TUNE_REQ failed\n");
	locked_.store(false);
	return -EIO;
}

int S1udReceiver::SetStreamId()
{
	return 0;
}

void S1udReceiver::StreamWorker()
{
	SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST);
	msg_info("px4::s1ud::S1udReceiver::StreamWorker: thread started\n");

	constexpr size_t RAW_BUFFER_SIZE = 65536;
	std::vector<uint8_t> rx_buffer(RAW_BUFFER_SIZE);
	std::vector<uint8_t> ts_buffer(RAW_BUFFER_SIZE);

	while (!stop_worker_.load()) {
		size_t actual = 0;
		int rc = parent_.ReadStreamBulk(rx_buffer.data(), RAW_BUFFER_SIZE, 100, &actual);
		if (rc != 0 || actual == 0) {
			std::this_thread::sleep_for(std::chrono::milliseconds(5));
			continue;
		}

		if (actual >= sizeof(sms_msg_hdr)) {
			const sms_msg_hdr *hdr = reinterpret_cast<const sms_msg_hdr*>(rx_buffer.data());
			if (hdr->msg_type == MSG_SMS_DVBT_BDA_DATA || hdr->msg_type == MSG_SMS_DATA_MSG) {
				size_t header_len = sizeof(sms_msg_hdr);
				if (actual > header_len) {
					ts_framer_.FeedRawData(rx_buffer.data() + header_len, actual - header_len);
				}
			} else {
				ts_framer_.FeedRawData(rx_buffer.data(), actual);
			}
		} else {
			ts_framer_.FeedRawData(rx_buffer.data(), actual);
		}

		while (ts_framer_.GetReadyCount() > 0) {
			size_t remain = 0;
			size_t popped = ts_framer_.PopTsPackets(ts_buffer.data(), ts_buffer.size(), &remain);
			if (popped > 0) {
				stream_buf_->Write(ts_buffer.data(), popped);
				stream_buf_->NotifyWrite();
			}
		}
	}

	msg_info("px4::s1ud::S1udReceiver::StreamWorker: thread stopped\n");
}

} // namespace px4::s1ud
