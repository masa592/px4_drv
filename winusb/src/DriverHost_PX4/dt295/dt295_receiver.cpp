// SPDX-License-Identifier: GPL-2.0-only
#include "dt295_receiver.hpp"
#include "dt295_device.hpp"
#include "../msg.h"

#include <algorithm>
#include <chrono>
#include <cstring>

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
	std::lock_guard<std::recursive_mutex> lock(lock_);
	msg_info("px4::dt295::Dt295Receiver::Init (index=%" PRIuPTR ")\n", index_);

	std::size_t stream_size = 188 * 4096;
	stream_buf_->Alloc(stream_size);
	stream_buf_->SetThresholdSize(stream_size / 10);

	routes_ = dt295_routes_create();
	if (!routes_) {
		msg_err("px4::dt295::Dt295Receiver::Init: dt295_routes_create failed\n");
		return -ENOMEM;
	}

	return 0;
}

void Dt295Receiver::Term()
{
	std::lock_guard<std::recursive_mutex> lock(lock_);
	if (open_.load()) {
		Close();
	}

	if (routes_) {
		dt295_routes_destroy(routes_);
		routes_ = nullptr;
	}

	msg_info("px4::dt295::Dt295Receiver::Term (index=%" PRIuPTR ")\n", index_);
}

int Dt295Receiver::Open()
{
	std::lock_guard<std::recursive_mutex> lock(lock_);
	msg_info("px4::dt295::Dt295Receiver::Open\n");
	open_.store(true);
	locked_.store(false);
	return 0;
}

void Dt295Receiver::Close()
{
	std::lock_guard<std::recursive_mutex> lock(lock_);
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
	std::lock_guard<std::recursive_mutex> lock(lock_);
	lnb_voltage_ = voltage;
	msg_info("px4::dt295::Dt295Receiver::SetLnbVoltage: %d V (PIX-DT295 note: external LNB power required)\n", voltage);
	return 0;
}

int Dt295Receiver::StartCaptureHardware()
{
	std::lock_guard<std::recursive_mutex> io_lock(parent_.GetIoMutex());
	::dt295 *device = parent_.GetDt295Handle();
	if (!device)
		return -ENODEV;

	if (hardware_started_)
		return 0;

	char error_buf[256] = {};

	dt295_route_plan desired{};
	char route_err[256] = {};
	if (dt295_routes_bootstrap_plan(routes_, &live_plan_, &desired, route_err) == 0) {
		int prep_rc = key_retained_ ?
			dt295_reprepare_plan(device, &desired, error_buf, sizeof(error_buf)) :
			dt295_prepare_plan(device, &desired, error_buf, sizeof(error_buf));

		if (prep_rc != 0) {
			msg_warn("px4::dt295::Dt295Receiver::StartCaptureHardware: prepare plan warning: %s\n", error_buf);
		} else {
			live_plan_ = desired;
			key_retained_ = true;
		}
	}

	if (dt295_start(device, error_buf, sizeof(error_buf)) != 0) {
		msg_err("px4::dt295::Dt295Receiver::StartCaptureHardware: dt295_start failed: %s\n", error_buf);
		return -EIO;
	}

	hardware_started_ = true;
	return 0;
}

int Dt295Receiver::StopCaptureHardware()
{
	std::lock_guard<std::recursive_mutex> io_lock(parent_.GetIoMutex());
	::dt295 *device = parent_.GetDt295Handle();
	if (!device || !hardware_started_)
		return 0;

	char error_buf[256] = {};
	size_t drained = 0;
	dt295_stop(device, &drained, error_buf, sizeof(error_buf));
	hardware_started_ = false;
	return 0;
}

int Dt295Receiver::SetCapture(bool capture)
{
	std::lock_guard<std::recursive_mutex> lock(lock_);

	if (capturing_.load() == capture)
		return 0;

	if (capture) {
		msg_info("px4::dt295::Dt295Receiver::SetCapture(true)\n");
		stream_buf_->Start();
		stream_buf_->Purge();

		if (StartCaptureHardware() != 0) {
			stream_buf_->Stop();
			return -EIO;
		}

		stop_worker_.store(false);
		capturing_.store(true);
		worker_thread_ = std::make_unique<std::thread>(&Dt295Receiver::StreamWorker, this);
	} else {
		msg_info("px4::dt295::Dt295Receiver::SetCapture(false)\n");
		capturing_.store(false);
		stop_worker_.store(true);

		if (worker_thread_ && worker_thread_->joinable()) {
			worker_thread_->join();
			worker_thread_.reset();
		}

		StopCaptureHardware();
		stream_buf_->Stop();
	}

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
	const bool was_capturing = capturing_.load();
	if (was_capturing) {
		capturing_.store(false);
		stop_worker_.store(true);
		if (worker_thread_ && worker_thread_->joinable()) {
			worker_thread_->join();
			worker_thread_.reset();
		}
	}

	std::lock_guard<std::recursive_mutex> io_lock(parent_.GetIoMutex());
	::dt295 *device = parent_.GetDt295Handle();
	if (!device) {
		msg_err("px4::dt295::Dt295Receiver::SetFrequency: device handle is null\n");
		return -ENODEV;
	}

	if (was_capturing) {
		char stop_err[256] = {};
		size_t drained = 0;
		dt295_stop(device, &drained, stop_err, sizeof(stop_err));
		hardware_started_ = false;
		stream_buf_->Stop();
	}

	char error_buf[256] = {};
	int rc = 0;

	if (routes_) {
		dt295_routes_reset(routes_);
		dt295_routes_set_satellite(routes_, params_.system != px4::SystemType::ISDB_T);
	}
	memset(&live_plan_, 0, sizeof(live_plan_));

	if (params_.system == px4::SystemType::ISDB_T) {
		unsigned channel = 0;
		if (params_.freq >= 473143 && params_.freq <= 767143) {
			channel = 13 + (params_.freq - 473143) / 6000;
		} else if (params_.freq >= 93143 && params_.freq <= 767143) {
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
	} else if (params_.system == px4::SystemType::ISDB_S) {
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
	} else {
		msg_err("px4::dt295::Dt295Receiver::SetFrequency: unsupported system\n");
		return -EINVAL;
	}

	if (was_capturing) {
		stream_buf_->Start();
		stream_buf_->Purge();
		dt295_route_plan desired{};
		char route_err[256] = {};
		if (dt295_routes_bootstrap_plan(routes_, &live_plan_, &desired, route_err) == 0) {
			int prep_rc = key_retained_ ?
				dt295_reprepare_plan(device, &desired, error_buf, sizeof(error_buf)) :
				dt295_prepare_plan(device, &desired, error_buf, sizeof(error_buf));
			if (prep_rc == 0) {
				live_plan_ = desired;
				key_retained_ = true;
			}
		}
		if (dt295_start(device, error_buf, sizeof(error_buf)) == 0) {
			hardware_started_ = true;
			stop_worker_.store(false);
			capturing_.store(true);
			worker_thread_ = std::make_unique<std::thread>(&Dt295Receiver::StreamWorker, this);
		}
	}

	return 0;
}

int Dt295Receiver::SetStreamId()
{
	std::lock_guard<std::recursive_mutex> io_lock(parent_.GetIoMutex());
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

void Dt295Receiver::StreamWorker()
{
	SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST);
	msg_info("px4::dt295::Dt295Receiver::StreamWorker: thread started\n");

	const size_t raw_capacity = 65536 * 2;
	std::vector<uint8_t> raw_buf(raw_capacity);
	std::vector<uint8_t> plain_chunk(65536);
	size_t buffered = 0;

	auto last_renew = std::chrono::steady_clock::now();

	while (!stop_worker_.load()) {
		::dt295 *device = parent_.GetDt295Handle();
		if (!device)
			break;

		auto now = std::chrono::steady_clock::now();
		if (std::chrono::duration_cast<std::chrono::seconds>(now - last_renew).count() >= 5) {
			std::lock_guard<std::recursive_mutex> io_lock(parent_.GetIoMutex());
			int renewed = 0;
			char err[256] = {};
			dt295_renew_if_due(device, &renewed, err, sizeof(err));
			last_renew = now;
		}

		size_t actual = 0;
		int rc = parent_.ReadStreamBulk(raw_buf.data() + buffered, 65536, 100, &actual);
		if (rc != 0 && actual == 0) {
			std::this_thread::sleep_for(std::chrono::milliseconds(5));
			continue;
		}

		buffered += actual;

		// Sync byte hunting (find 0x47 sync byte)
		size_t offset = 0;
		while (offset + 188 <= buffered) {
			if (raw_buf[offset] == 0x47) {
				// Potential TS sync byte found
				break;
			}
			offset++;
		}

		if (offset > 0) {
			buffered -= offset;
			if (buffered > 0) {
				std::memmove(raw_buf.data(), raw_buf.data() + offset, buffered);
			}
		}

		size_t length = (buffered / 188) * 188;
		size_t emitted = 0;

		for (size_t at = 0; at < length; at += 188) {
			uint8_t *packet = raw_buf.data() + at;

			if (packet[0] == 0x47) {
				// Decrypt TS packet with DT295 session key
				dt295_decode_packet(device, packet);

				if (routes_) {
					dt295_routes_feed(routes_, packet);
				}

				if (plain_chunk.size() < emitted + 188) {
					plain_chunk.resize(emitted + 65536);
				}
				std::memcpy(plain_chunk.data() + emitted, packet, 188);
				emitted += 188;
			}
		}

		buffered -= length;
		if (buffered > 0) {
			std::memmove(raw_buf.data(), raw_buf.data() + length, buffered);
		}

		if (emitted > 0) {
			stream_buf_->Write(plain_chunk.data(), emitted);
			stream_buf_->NotifyWrite();
		}

		// Update route plan dynamically via dt295_route_write
		if (routes_ && length > 0) {
			dt295_routes_snapshot snap{};
			dt295_routes_snapshot_get(routes_, &snap);
			if (snap.ready && !snap.unsupported_tables) {
				dt295_route_plan desired{};
				char route_err[256] = {};
				if (dt295_routes_build(routes_, 0, &live_plan_, &desired, route_err) == 0) {
					dt295_route_op ops[DT295_ROUTE_OP_LIMIT];
					size_t op_count = 0;
					if (dt295_routes_plan_ops(&live_plan_, &desired, ops, &op_count) == 0 && op_count > 0) {
						std::lock_guard<std::recursive_mutex> io_lock(parent_.GetIoMutex());
						char route_write_err[256] = {};
						bool ok = true;
						for (size_t oi = 0; oi < op_count; ++oi) {
							if (dt295_route_write(device, &ops[oi], route_write_err, sizeof(route_write_err)) != 0) {
								msg_warn("px4::dt295::Dt295Receiver::StreamWorker: dt295_route_write failed: %s\n", route_write_err);
								ok = false;
								break;
							}
						}
						if (ok) {
							dt295_routes_commit(&live_plan_, &desired);
						}
					}
				}
			}
		}
	}

	msg_info("px4::dt295::Dt295Receiver::StreamWorker: thread stopped\n");
}

} // namespace px4::dt295
