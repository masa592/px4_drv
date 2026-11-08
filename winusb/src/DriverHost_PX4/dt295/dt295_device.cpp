// SPDX-License-Identifier: GPL-2.0-only
#include "dt295_device.hpp"
#include "dt295_receiver.hpp"
#include "../receiver_manager.hpp"
#include "../msg.h"

#include <chrono>
#include <fstream>
#include <thread>
#include <vector>

namespace px4::dt295 {

Dt295Device::Dt295Device(const std::wstring &path, const px4::DeviceDefinition &definition,
	std::uintptr_t index, px4::ReceiverManager &manager)
	: px4::DeviceBase(path, definition, index, manager)
{
	msg_info("px4::dt295::Dt295Device created for path: %ls\n", path.c_str());

	io_.user = this;
	io_.control = ControlCallback;
	io_.bulk = BulkCallback;
	io_.interrupt = InterruptCallback;
	io_.now_ms = NowMsCallback;
	io_.sleep_ms = SleepMsCallback;
}

Dt295Device::~Dt295Device()
{
	Term();
}

std::wstring Dt295Device::FindFirmwarePath() const
{
	wchar_t mod_path[MAX_PATH] = {};
	if (GetModuleFileNameW(nullptr, mod_path, MAX_PATH)) {
		wchar_t *last_slash = wcsrchr(mod_path, L'\\');
		if (last_slash)
			*(last_slash + 1) = L'\0';
		std::wstring candidate = std::wstring(mod_path) + L"dt295.bin";
		DWORD attr = GetFileAttributesW(candidate.c_str());
		if (attr != INVALID_FILE_ATTRIBUTES && !(attr & FILE_ATTRIBUTE_DIRECTORY))
			return candidate;
	}
	return L"dt295.bin";
}

int Dt295Device::Init()
{
	std::lock_guard<std::recursive_mutex> lock(io_mutex_);

	std::wstring fw_path = FindFirmwarePath();
	msg_info("px4::dt295::Dt295Device::Init: loading firmware from %ls\n", fw_path.c_str());

	std::ifstream file(fw_path, std::ios::binary | std::ios::ate);
	if (!file.is_open()) {
		msg_err("px4::dt295::Dt295Device::Init: failed to open firmware %ls\n", fw_path.c_str());
		return -1;
	}

	std::streamsize size = file.tellg();
	file.seekg(0, std::ios::beg);
	std::vector<uint8_t> buffer(static_cast<size_t>(size));
	if (!file.read(reinterpret_cast<char*>(buffer.data()), size)) {
		msg_err("px4::dt295::Dt295Device::Init: failed to read firmware %ls\n", fw_path.c_str());
		return -1;
	}

	char error_buf[256] = {};
	int rc = dt295_assets_load_bytes(buffer.data(), buffer.size(), &assets_, error_buf, sizeof(error_buf));
	if (rc != 0 || !assets_) {
		msg_err("px4::dt295::Dt295Device::Init: dt295_assets_load_bytes failed: %s\n", error_buf);
		return -1;
	}

	rc = dt295_create(&dt295_handle_, &io_, assets_, error_buf, sizeof(error_buf));
	if (rc != 0 || !dt295_handle_) {
		msg_err("px4::dt295::Dt295Device::Init: dt295_create failed: %s\n", error_buf);
		dt295_assets_free(assets_);
		assets_ = nullptr;
		return -1;
	}

	msg_info("px4::dt295::Dt295Device::Init: executing dt295_boot\n");
	rc = dt295_boot(dt295_handle_, error_buf, sizeof(error_buf));
	if (rc != 0) {
		msg_err("px4::dt295::Dt295Device::Init: dt295_boot failed: %s\n", error_buf);
		Term();
		return -1;
	}

	msg_info("px4::dt295::Dt295Device::Init: executing dt295_open_session\n");
	rc = dt295_open_session(dt295_handle_, error_buf, sizeof(error_buf));
	if (rc != 0) {
		msg_err("px4::dt295::Dt295Device::Init: dt295_open_session failed: %s\n", error_buf);
		Term();
		return -1;
	}

	msg_info("px4::dt295::Dt295Device::Init: executing dt295_card_initialize\n");
	rc = dt295_card_initialize(dt295_handle_, error_buf, sizeof(error_buf));
	if (rc != 0) {
		msg_warn("px4::dt295::Dt295Device::Init: dt295_card_initialize warning: %s\n", error_buf);
	} else {
		msg_info("px4::dt295::Dt295Device::Init: executing dt295_card_activate\n");
		rc = dt295_card_activate(dt295_handle_, 2000, error_buf, sizeof(error_buf));
		if (rc != 0) {
			msg_warn("px4::dt295::Dt295Device::Init: dt295_card_activate warning: %s\n", error_buf);
		}
	}

	receiver_ = std::make_unique<Dt295Receiver>(*this, 0);
	if (receiver_->Init() != 0) {
		msg_err("px4::dt295::Dt295Device::Init: failed to init Dt295Receiver\n");
		Term();
		return -1;
	}

	for (const auto &rcvr_def : device_def_.receivers) {
		px4::command::ReceiverInfo ri{};
		ri.device_guid = device_def_.guid;
		wcscpy_s(ri.receiver_name, rcvr_def.name.c_str());
		ri.receiver_guid = rcvr_def.guid;
		ri.systems = rcvr_def.systems;
		ri.index = rcvr_def.index;
		ri.data_id = 0;
		receiver_manager_.Register(ri, receiver_.get());
	}

	registered_ = true;
	msg_info("px4::dt295::Dt295Device::Init: PIX-DT295 boot and initialization succeeded\n");
	return 0;
}

void Dt295Device::Term()
{
	if (receiver_) {
		receiver_manager_.Unregister(receiver_.get());
		receiver_->Term();
		receiver_.reset();
	}
	std::lock_guard<std::recursive_mutex> lock(io_mutex_);

	if (dt295_handle_) {
		char error_buf[256] = {};
		if (dt295_has_session(dt295_handle_)) {
			size_t drained = 0;
			dt295_stop(dt295_handle_, &drained, error_buf, sizeof(error_buf));
			dt295_shutdown(dt295_handle_, error_buf, sizeof(error_buf));
		}
		dt295_destroy(dt295_handle_);
		dt295_handle_ = nullptr;
	}

	if (assets_) {
		dt295_assets_free(assets_);
		assets_ = nullptr;
	}

	registered_ = false;
	msg_info("px4::dt295::Dt295Device::Term completed\n");
}

void Dt295Device::SetAvailability(bool available)
{
	available_.store(available);
}

px4::ReceiverBase *Dt295Device::GetReceiver(int id) const
{
	if (id == 0)
		return receiver_.get();
	return nullptr;
}

int Dt295Device::ReadStreamBulk(uint8_t *data, size_t length, unsigned timeout_ms, size_t *actual)
{
	WINUSB_INTERFACE_HANDLE winusb = usb_dev_.winusb;
	if (!winusb)
		return -1;

	WinUsb_SetPipePolicy(winusb, 0x81, PIPE_TRANSFER_TIMEOUT, sizeof(timeout_ms), &timeout_ms);
	ULONG transferred = 0;
	BOOL ok = WinUsb_ReadPipe(winusb, 0x81, data, static_cast<ULONG>(length), &transferred, nullptr);
	if (actual)
		*actual = transferred;
	return ok ? 0 : -1;
}

int Dt295Device::ControlCallback(void *user, uint8_t request_type, uint8_t request, uint16_t value,
	uint16_t index, uint8_t *data, size_t length, unsigned timeout_ms, size_t *actual)
{
	auto self = static_cast<Dt295Device*>(user);
	WINUSB_INTERFACE_HANDLE winusb = self->usb_dev_.winusb;
	if (!winusb)
		return -1;

	WINUSB_SETUP_PACKET setup{};
	setup.RequestType = request_type;
	setup.Request = request;
	setup.Value = value;
	setup.Index = index;
	setup.Length = static_cast<USHORT>(length);

	WinUsb_SetPipePolicy(winusb, 0x00, PIPE_TRANSFER_TIMEOUT, sizeof(timeout_ms), &timeout_ms);

	ULONG transferred = 0;
	BOOL ok = WinUsb_ControlTransfer(winusb, setup, data, static_cast<ULONG>(length), &transferred, nullptr);
	if (actual)
		*actual = transferred;

	return ok ? 0 : -1;
}

int Dt295Device::BulkCallback(void *user, uint8_t endpoint, uint8_t *data, size_t length,
	unsigned timeout_ms, size_t *actual)
{
	auto self = static_cast<Dt295Device*>(user);
	WINUSB_INTERFACE_HANDLE winusb = self->usb_dev_.winusb;
	if (!winusb)
		return -1;

	WinUsb_SetPipePolicy(winusb, endpoint, PIPE_TRANSFER_TIMEOUT, sizeof(timeout_ms), &timeout_ms);

	ULONG transferred = 0;
	BOOL ok = FALSE;

	DWORD err = ERROR_SUCCESS;
	if (endpoint & 0x80) {
		ok = WinUsb_ReadPipe(winusb, endpoint, data, static_cast<ULONG>(length), &transferred, nullptr);
		if (!ok)
			err = GetLastError();
	} else {
		ok = WinUsb_WritePipe(winusb, endpoint, data, static_cast<ULONG>(length), &transferred, nullptr);
		if (!ok)
			err = GetLastError();
	}

	if (actual)
		*actual = transferred;

	if (ok)
		return 0;
	if (err == ERROR_SEM_TIMEOUT || err == ERROR_TIMEOUT)
		return -7; // USB_TIMEOUT
	return -1;
}

int Dt295Device::InterruptCallback(void *user, uint8_t endpoint, uint8_t *data, size_t length,
	unsigned timeout_ms, size_t *actual)
{
	return BulkCallback(user, endpoint, data, length, timeout_ms, actual);
}

uint64_t Dt295Device::NowMsCallback(void *user)
{
	(void)user;
	return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
		std::chrono::steady_clock::now().time_since_epoch()).count());
}

void Dt295Device::SleepMsCallback(void *user, unsigned ms)
{
	(void)user;
	std::this_thread::sleep_for(std::chrono::milliseconds(ms));
}

int Dt295Device::OpenCard()
{
	return 0;
}

void Dt295Device::CloseCard()
{
}

int Dt295Device::DetectCard(bool &detected)
{
	detected = true;
	return 0;
}

int Dt295Device::ResetCard()
{
	return 0;
}

int Dt295Device::SetCardBaudrate(::it930x_uart_baudrate baudrate)
{
	(void)baudrate;
	return 0;
}

int Dt295Device::IsCardDataReady(bool &ready)
{
	ready = false;
	return 0;
}

int Dt295Device::ReadCardData(std::uint8_t *buf, std::uint8_t &len)
{
	(void)buf;
	len = 0;
	return 0;
}

int Dt295Device::WriteCardData(const std::uint8_t *buf, std::uint8_t len)
{
	(void)buf;
	(void)len;
	return 0;
}

} // namespace px4::dt295
