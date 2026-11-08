// SPDX-License-Identifier: GPL-2.0-only
#include "dt295_device.hpp"
#include "../receiver_manager.hpp"
#include "../msg.h"

namespace px4::dt295 {

Dt295Device::Dt295Device(const std::wstring &path, const px4::DeviceDefinition &definition,
	std::uintptr_t index, px4::ReceiverManager &manager)
	: px4::DeviceBase(path, definition, index, manager)
{
	msg_info("px4::dt295::Dt295Device created for path: %ls\n", path.c_str());
}

Dt295Device::~Dt295Device()
{
	Term();
}

int Dt295Device::Init()
{
	msg_info("px4::dt295::Dt295Device::Init: detected PIX-DT295 at %ls\n", path_.c_str());
	// Phase 1: Device detection succeeded
	registered_ = true;
	return 0;
}

void Dt295Device::Term()
{
	if (!registered_)
		return;
	registered_ = false;
	msg_info("px4::dt295::Dt295Device::Term\n");
}

void Dt295Device::SetAvailability(bool available)
{
	available_.store(available);
}

px4::ReceiverBase *Dt295Device::GetReceiver(int id) const
{
	(void)id;
	return nullptr;
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
