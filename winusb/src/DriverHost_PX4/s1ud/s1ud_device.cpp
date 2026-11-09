// SPDX-License-Identifier: GPL-2.0-only
#define _CRT_SECURE_NO_WARNINGS
#include "s1ud_device.hpp"
#include "s1ud_receiver.hpp"
#include "../receiver_manager.hpp"
#include "msg.h"

#include <algorithm>
#include <fstream>
#include <vector>

namespace px4::s1ud {

S1udDevice::S1udDevice(const std::wstring &path, const px4::DeviceDefinition &definition,
	std::uintptr_t index, px4::ReceiverManager &manager)
	: px4::DeviceBase(path, definition, index, manager)
{
	msg_info("px4::s1ud::S1udDevice created for path: %ls\n", path.c_str());

	// Parse endpoints
	WINUSB_INTERFACE_HANDLE winusb = usb_dev_.winusb;
	if (winusb) {
		USB_INTERFACE_DESCRIPTOR intfDesc = {};
		if (WinUsb_QueryInterfaceSettings(winusb, 0, &intfDesc)) {
			for (UCHAR i = 0; i < intfDesc.bNumEndpoints; ++i) {
				WINUSB_PIPE_INFORMATION pipeInfo = {};
				if (WinUsb_QueryPipe(winusb, 0, i, &pipeInfo)) {
					if (pipeInfo.PipeType == UsbdPipeTypeBulk) {
						if (USB_ENDPOINT_DIRECTION_IN(pipeInfo.PipeId)) {
							in_ep_ = pipeInfo.PipeId;
						} else {
							out_ep_ = pipeInfo.PipeId;
						}
					}
				}
			}
		}

		ULONG timeout_val = 2000;
		WinUsb_SetPipePolicy(winusb, in_ep_, PIPE_TRANSFER_TIMEOUT, sizeof(timeout_val), &timeout_val);
		WinUsb_SetPipePolicy(winusb, out_ep_, PIPE_TRANSFER_TIMEOUT, sizeof(timeout_val), &timeout_val);
		UCHAR auto_clear = 1;
		WinUsb_SetPipePolicy(winusb, in_ep_, AUTO_CLEAR_STALL, sizeof(auto_clear), &auto_clear);
		WinUsb_SetPipePolicy(winusb, out_ep_, AUTO_CLEAR_STALL, sizeof(auto_clear), &auto_clear);
	}
}

S1udDevice::~S1udDevice()
{
	Term();
}

std::wstring S1udDevice::FindFirmwarePath() const
{
	wchar_t mod_path[MAX_PATH] = {};
	if (GetModuleFileNameW(nullptr, mod_path, MAX_PATH)) {
		wchar_t *last_slash = wcsrchr(mod_path, L'\\');
		if (last_slash)
			*(last_slash + 1) = L'\0';

		std::wstring cand1 = std::wstring(mod_path) + L"isdbt_rio.inp";
		if (GetFileAttributesW(cand1.c_str()) != INVALID_FILE_ATTRIBUTES)
			return cand1;

		std::wstring cand2 = std::wstring(mod_path) + L"isdbt_nova_12mhz_b0.inp";
		if (GetFileAttributesW(cand2.c_str()) != INVALID_FILE_ATTRIBUTES)
			return cand2;
	}

	if (GetFileAttributesW(L"isdbt_rio.inp") != INVALID_FILE_ATTRIBUTES)
		return L"isdbt_rio.inp";

	return L"isdbt_nova_12mhz_b0.inp";
}

int S1udDevice::Init()
{
	std::lock_guard<std::recursive_mutex> lock(io_mutex_);

	int current_mode = DEVICE_MODE_NONE;
	DetectMode(current_mode);
	msg_info("px4::s1ud::S1udDevice::Init: current Siano mode=%d\n", current_mode);

	if (current_mode != DEVICE_MODE_ISDBT && current_mode != DEVICE_MODE_ISDBT_BDA) {
		std::wstring fw_path = FindFirmwarePath();
		msg_info("px4::s1ud::S1udDevice::Init: loading firmware from %ls\n", fw_path.c_str());
		if (!LoadFirmware(fw_path)) {
			msg_warn("px4::s1ud::S1udDevice::Init: LoadFirmware failed or file not found, checking if device already ready\n");
		}
		InitDevice(DEVICE_MODE_ISDBT_BDA);
	}

	receiver_ = std::make_unique<S1udReceiver>(*this, 0);
	if (receiver_->Init() != 0) {
		msg_err("px4::s1ud::S1udDevice::Init: failed to init S1udReceiver\n");
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
	msg_info("px4::s1ud::S1udDevice::Init: PX-S1UD initialization succeeded\n");
	return 0;
}

void S1udDevice::Term()
{
	if (receiver_) {
		receiver_manager_.Unregister(receiver_.get());
		receiver_->Term();
		receiver_.reset();
	}

	registered_ = false;
	msg_info("px4::s1ud::S1udDevice::Term completed\n");
}

void S1udDevice::SetAvailability(bool available)
{
	available_.store(available);
}

px4::ReceiverBase *S1udDevice::GetReceiver(int id) const
{
	if (id == 0)
		return receiver_.get();
	return nullptr;
}

bool S1udDevice::SendCommand(const void *cmd, size_t length)
{
	WINUSB_INTERFACE_HANDLE winusb = usb_dev_.winusb;
	if (!winusb) return false;
	ULONG written = 0;
	return WinUsb_WritePipe(winusb, out_ep_, const_cast<PUCHAR>(static_cast<const uint8_t*>(cmd)), static_cast<ULONG>(length), &written, nullptr) == TRUE;
}

bool S1udDevice::SendCommandAndWaitResponse(const void *cmd, size_t length, uint16_t expected_res_type, void *res_buf, size_t res_buf_size, DWORD timeout_ms)
{
	WINUSB_INTERFACE_HANDLE winusb = usb_dev_.winusb;
	if (!winusb) return false;

	if (!SendCommand(cmd, length)) {
		return false;
	}

	DWORD start_time = GetTickCount();
	while (GetTickCount() - start_time < timeout_ms) {
		uint8_t buffer[2048] = {};
		ULONG read_bytes = 0;
		if (WinUsb_ReadPipe(winusb, in_ep_, buffer, sizeof(buffer), &read_bytes, nullptr) && read_bytes >= sizeof(sms_msg_hdr)) {
			const sms_msg_hdr *hdr = reinterpret_cast<const sms_msg_hdr*>(buffer);
			if (hdr->msg_type == expected_res_type) {
				if (res_buf && res_buf_size > 0) {
					size_t copy_size = (std::min)(static_cast<size_t>(read_bytes), res_buf_size);
					std::memcpy(res_buf, buffer, copy_size);
				}
				return true;
			}
		}
		Sleep(10);
	}
	return false;
}

bool S1udDevice::DetectMode(int &current_mode)
{
	current_mode = DEVICE_MODE_NONE;
	sms_msg_hdr req = {};
	InitSianoMsgHdr(&req, MSG_SMS_GET_VERSION_EX_REQ, 0, HIF_TASK, sizeof(req));

	sms_version_res res = {};
	if (SendCommandAndWaitResponse(&req, sizeof(req), MSG_SMS_GET_VERSION_EX_RES, &res, sizeof(res), 1500)) {
		current_mode = res.firmware_id == 255 ? DEVICE_MODE_NONE : res.firmware_id;
		return true;
	}
	return false;
}

bool S1udDevice::LoadFirmware(const std::wstring &custom_fw_path)
{
	std::vector<std::wstring> candidates;
	if (!custom_fw_path.empty()) {
		candidates.push_back(custom_fw_path);
	}
	candidates.push_back(L"isdbt_rio.inp");
	candidates.push_back(L"isdbt_nova_12mhz_b0.inp");

	std::vector<uint8_t> fw_data;
	for (const auto &path : candidates) {
		std::ifstream file(path, std::ios::binary | std::ios::ate);
		if (file.is_open()) {
			std::streamsize size = file.tellg();
			file.seekg(0, std::ios::beg);
			fw_data.resize(static_cast<size_t>(size));
			if (file.read(reinterpret_cast<char*>(fw_data.data()), size)) {
				break;
			}
			fw_data.clear();
		}
	}

	if (fw_data.empty()) {
		return false;
	}

	return SendFirmwareData(fw_data.data(), fw_data.size());
}

bool S1udDevice::SendFirmwareData(const uint8_t *fw_data, size_t fw_size)
{
	if (!fw_data || fw_size < sizeof(sms_firmware_header)) return false;

	const sms_firmware_header *fw_hdr = reinterpret_cast<const sms_firmware_header*>(fw_data);
	uint32_t start_addr = fw_hdr->start_address;
	uint32_t length = fw_hdr->length;
	const uint8_t *payload = fw_data + sizeof(sms_firmware_header);
	size_t payload_len = fw_size - sizeof(sms_firmware_header);

	// 1. Reload start
	sms_msg_hdr reload_req = {};
	InitSianoMsgHdr(&reload_req, MSG_SW_RELOAD_START_REQ, 0, HIF_TASK, sizeof(reload_req));
	sms_msg_hdr reload_res = {};
	SendCommandAndWaitResponse(&reload_req, sizeof(reload_req), MSG_SW_RELOAD_START_RES, &reload_res, sizeof(reload_res), 1000);

	// 2. Download chunks
	uint32_t current_addr = start_addr;
	size_t remain = payload_len;
	const uint8_t *cur_payload = payload;

	while (remain > 0) {
		size_t chunk_size = (std::min)(remain, static_cast<size_t>(SMS_MAX_PAYLOAD_SIZE));
		sms_data_download dl_msg = {};
		InitSianoMsgHdr(&dl_msg.x_msg_header, MSG_SMS_DATA_DOWNLOAD_REQ, 0, HIF_TASK,
			static_cast<uint16_t>(sizeof(sms_msg_hdr) + sizeof(uint32_t) + chunk_size));
		dl_msg.mem_addr = current_addr;
		std::memcpy(dl_msg.payload, cur_payload, chunk_size);

		sms_msg_hdr dl_res = {};
		if (!SendCommandAndWaitResponse(&dl_msg, dl_msg.x_msg_header.msg_length, MSG_SMS_DATA_DOWNLOAD_RES, &dl_res, sizeof(dl_res), 1000)) {
			return false;
		}

		cur_payload += chunk_size;
		current_addr += static_cast<uint32_t>(chunk_size);
		remain -= chunk_size;
	}

	// 3. Validity check
	struct {
		sms_msg_hdr hdr;
		uint32_t data[3];
	} val_msg = {};
	InitSianoMsgHdr(&val_msg.hdr, MSG_SMS_DATA_VALIDITY_REQ, 0, HIF_TASK, sizeof(val_msg));
	val_msg.data[0] = start_addr;
	val_msg.data[1] = length;
	val_msg.data[2] = 0;

	sms_msg_data val_res = {};
	if (!SendCommandAndWaitResponse(&val_msg, sizeof(val_msg), MSG_SMS_DATA_VALIDITY_RES, &val_res, sizeof(val_res), 1500)) {
		return false;
	}

	// 4. Trigger / Reload exec
	sms_msg_data5 trig_msg = {};
	InitSianoMsgHdr(&trig_msg.x_msg_header, MSG_SMS_SWDOWNLOAD_TRIGGER_REQ, 0, HIF_TASK, sizeof(trig_msg));
	trig_msg.msg_data[0] = start_addr;
	trig_msg.msg_data[1] = 6;     // Priority
	trig_msg.msg_data[2] = 0x200; // Stack size
	trig_msg.msg_data[3] = 0;     // Parameter
	trig_msg.msg_data[4] = 4;     // Task ID

	sms_msg_hdr trig_res = {};
	SendCommandAndWaitResponse(&trig_msg, sizeof(trig_msg), MSG_SMS_SWDOWNLOAD_TRIGGER_RES, &trig_res, sizeof(trig_res), 1000);

	Sleep(400);
	return true;
}

bool S1udDevice::InitDevice(int mode)
{
	sms_msg_data msg = {};
	InitSianoMsgHdr(&msg.x_msg_header, MSG_SMS_INIT_DEVICE_REQ, DVBT_BDA_CONTROL_MSG_ID, HIF_TASK, sizeof(msg));
	msg.msg_data = mode;

	sms_msg_hdr res = {};
	return SendCommandAndWaitResponse(&msg, sizeof(msg), MSG_SMS_INIT_DEVICE_RES, &res, sizeof(res), 1500);
}

int S1udDevice::ReadStreamBulk(uint8_t *data, size_t length, unsigned timeout_ms, size_t *actual)
{
	WINUSB_INTERFACE_HANDLE winusb = usb_dev_.winusb;
	if (!winusb)
		return -1;

	WinUsb_SetPipePolicy(winusb, in_ep_, PIPE_TRANSFER_TIMEOUT, sizeof(timeout_ms), &timeout_ms);
	ULONG transferred = 0;
	BOOL ok = WinUsb_ReadPipe(winusb, in_ep_, data, static_cast<ULONG>(length), &transferred, nullptr);
	if (actual)
		*actual = transferred;
	return ok ? 0 : -1;
}

} // namespace px4::s1ud
