#include "stdafx.h"
#include "SDHRNetworker.h"
#include <stdint.h>
#include "Memory.h"
#include "Interface.h"
#include "Configuration/IPropertySheet.h"
#include "Registry.h"
#include "Log.h"
#include <iostream>

constexpr uint16_t cxSDHR_ctrl = 0xC0A0;	// SDHR command
constexpr uint16_t cxSDHR_data = 0xC0A1;	// SDHR data

SDHRNetworker::SDHRNetworker()
{
	if (IsEnabled())
	{
		Connect();
		BusCtrlPacket(SDHRCtrl_e::SDHR_CTRL_RESET);
	}
}

SDHRNetworker::~SDHRNetworker()
{
	Disconnect();
	client_socket = NULL;
}

bool SDHRNetworker::IsEnabled()
{
	DWORD _sdhrIsEnabled = 0;
	REGLOAD_DEFAULT(TEXT(REGVALUE_SDHR_REMOTE_ENABLED), &_sdhrIsEnabled, 0);
	return (_sdhrIsEnabled != 0);
}

void SDHRNetworker::Disconnect()
{
	if (client_socket > 0)
		closesocket(client_socket);
}

bool SDHRNetworker::Connect()
{
	return SDHRNetworker::Connect("", 0);
}

bool SDHRNetworker::Connect(std::string server_ip, int server_port)
{
	Disconnect();

	if (!IsEnabled())
		return false;

	DWORD _sdhrPort = 0;
	RegLoadValue(TEXT(REG_PREFS), TEXT(REGVALUE_SDHR_REMOTE_PORT), 1, &_sdhrPort);

	CHAR _remoteIp[16] = "";
	RegLoadString(TEXT(REG_PREFS), TEXT(REGVALUE_SDHR_REMOTE_IP), 1, _remoteIp, 15);

	server_addr.sin_family = AF_INET;
	memset(&(server_addr.sin_zero), '\0', 8);
	if (server_ip.length() != 0)
		server_addr.sin_addr.s_addr = inet_addr(server_ip.c_str());
	else
		server_addr.sin_addr.s_addr = inet_addr(_remoteIp);

	if (server_port != 0)
		server_addr.sin_port = htons(server_port);
	else
		server_addr.sin_port = htons(_sdhrPort);

	WSADATA wsaData;
	int result = WSAStartup(MAKEWORD(2, 2), &wsaData);
	if (result != 0) {
		throw std::runtime_error("WSAStartup failed: " + result);
		bIsConnected = false;
		return bIsConnected;
	}

	client_socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (client_socket == INVALID_SOCKET) {
		throw std::runtime_error("Error creating socket: " + WSAGetLastError());
		WSACleanup();
		bIsConnected = false;
		return bIsConnected;
	}
	if (connect(client_socket, (sockaddr*)&server_addr, sizeof(server_addr)) == SOCKET_ERROR) {
		std::cerr << std::dec << "Error connecting to server: " + WSAGetLastError() << std::endl;
		closesocket(client_socket);
		WSACleanup();
		bIsConnected = false;
		return bIsConnected;
	}
	bIsConnected = true;


	return bIsConnected;
}

// Individual updates are done simply (but inefficiently), using an entire 8-event packet
// to represent a single write event, and then 7 bytes of dummy reads from 0x0000

void SDHRNetworker::BusCtrlPacket(BYTE command)
{
	char buf[2048];
	++next_seqno;
	BusHeader* h = (BusHeader*)buf;
	h->seqno[0] = next_seqno & 0xff;
	h->seqno[1] = (next_seqno >> 8) & 0xff;
	h->seqno[2] = (next_seqno >> 16) & 0xff;
	h->seqno[3] = (next_seqno >> 24) & 0xff;
	h->cmdtype = 0;
	BusPacket* p = (BusPacket*)(buf + sizeof(BusHeader));
	p->rwflags = 0xfe;
	p->seqflags = 0xff;
	memset(p->data, 0, sizeof(p->data));
	p->data[0] = command;
	memset(p->addrs, 0, sizeof(p->addrs));
	p->addrs[0] = cxSDHR_ctrl & 0xff;
	p->addrs[1] = (cxSDHR_ctrl >> 8) & 0xff;
	send(client_socket, buf, sizeof(BusHeader) + sizeof(BusPacket), 0);
}

void SDHRNetworker::BusDataPacket(BYTE data)
{
	char buf[2048];
	++next_seqno;
	BusHeader* h = (BusHeader*)buf;
	h->seqno[0] = next_seqno & 0xff;
	h->seqno[1] = (next_seqno >> 8) & 0xff;
	h->seqno[2] = (next_seqno >> 16) & 0xff;
	h->seqno[3] = (next_seqno >> 24) & 0xff;
	h->cmdtype = 0;
	BusPacket* p = (BusPacket*)(buf + sizeof(BusHeader));
	p->rwflags = 0xfe;
	p->seqflags = 0xff;
	memset(p->data, 0, sizeof(p->data));
	p->data[0] = data;
	memset(p->addrs, 0, sizeof(p->addrs));
	p->addrs[0] = cxSDHR_data & 0xff;
	p->addrs[1] = (cxSDHR_data >> 8) & 0xff;
	send(client_socket, buf, sizeof(BusHeader) + sizeof(BusPacket), 0);
}

void SDHRNetworker::BusMemoryPacket(BYTE data, WORD addr)
{
	char buf[2048];
	++next_seqno;
	BusHeader* h = (BusHeader*)buf;
	h->seqno[0] = next_seqno & 0xff;
	h->seqno[1] = (next_seqno >> 8) & 0xff;
	h->seqno[2] = (next_seqno >> 16) & 0xff;
	h->seqno[3] = (next_seqno >> 24) & 0xff;
	h->cmdtype = 0;
	BusPacket* p = (BusPacket*)(buf + sizeof(BusHeader));
	p->rwflags = 0xfe;
	p->seqflags = 0xff;
	memset(p->data, 0, sizeof(p->data));
	p->data[0] = data;
	memset(p->addrs, 0, sizeof(p->addrs));
	p->addrs[0] = addr & 0xff;
	p->addrs[1] = (addr >> 8) & 0xff;
	send(client_socket, buf, sizeof(BusHeader) + sizeof(BusPacket), 0);
}

//////////////////////////////////////////////////////////////////////////
// Streams
//////////////////////////////////////////////////////////////////////////

void SDHRNetworker::BusDataCommandStream(BYTE* data, int length)
{
	// fill the packets with 50 chunks, with dummy reads
	// at the end

	LogFileOutput("SDHRNetworker::BusDataCommandStream: %d %d \n", data, length);
	size_t numBlocks = (length + 7) / 8;
	size_t numPackets = (numBlocks + 49) / 50;
	char buf[2048];
	for (size_t packet = 0; packet < numPackets; ++packet) {
		++next_seqno;
		BusHeader* h = (BusHeader*)buf;
		h->seqno[0] = next_seqno & 0xff;
		h->seqno[1] = (next_seqno >> 8) & 0xff;
		h->seqno[2] = (next_seqno >> 16) & 0xff;
		h->seqno[3] = (next_seqno >> 24) & 0xff;
		h->cmdtype = 0;
		BusPacket* p = (BusPacket*)(buf + sizeof(BusHeader));
		for (size_t b = 0; b < 50; b++)
		{
			p->rwflags = 0;
			p->seqflags = 0xff;
			for (size_t i = 0; i < 8; i++)
			{
				if ((int)(packet * 50 + b * 8 + i) < length) {
					p->data[i] = data[packet * 50 + b * 8 + i];
					p->rwflags |= 0 << i;
					p->addrs[i * 2] = cxSDHR_data & 0xff;
					p->addrs[i * 2 + 1] = (cxSDHR_data >> 8) & 0xff;
				}
				else {
					p->data[i] = 0;
					p->rwflags |= 1 << i;
					p->addrs[i * 2] = 0;
					p->addrs[i * 2 + 1] = 0;
				}
			}
			++p;
		}
		send(client_socket, buf, sizeof(BusHeader) + sizeof(BusPacket) * 50, 0);
	}
}

void SDHRNetworker::BusDataMemoryStream(LPBYTE memPtr, WORD source_address, int length)
{
	// Send a bunch of SDHR_STREAM_CHUNK
// and the remaining packets individually

	LogFileOutput("SDHRNetworker::BusDataMemoryStream: %d %d %d \n", memPtr, source_address, length);
	size_t numBlocks = (length + 7) / 8;
	size_t numPackets = (numBlocks + 49) / 50;
	char buf[2048];
	for (size_t packet = 0; packet < numPackets; ++packet) {
		++next_seqno;
		BusHeader* h = (BusHeader*)buf;
		h->seqno[0] = next_seqno & 0xff;
		h->seqno[1] = (next_seqno >> 8) & 0xff;
		h->seqno[2] = (next_seqno >> 16) & 0xff;
		h->seqno[3] = (next_seqno >> 24) & 0xff;
		h->cmdtype = 0;
		BusPacket* p = (BusPacket*)(buf + sizeof(BusHeader));
		for (size_t b = 0; b < 50; b++)
		{
			p->rwflags = 0;
			p->seqflags = 0xff;
			for (size_t i = 0; i < 8; i++)
			{
				if ((int)(packet * 50 + b * 8 + i) < length) {
					p->data[i] = memPtr[packet * 50 + b * 8 + i];
					p->rwflags |= 0 << i;
					p->addrs[i * 2] = (source_address + packet * 50 + b * 8 + i) & 0xff;
					p->addrs[i * 2 + 1] = ((source_address + packet * 50 + b * 8 + i) >> 8) & 0xff;
				}
				else {
					p->data[i] = 0;
					p->rwflags |= 1 << i;
					p->addrs[i * 2] = 0;
					p->addrs[i * 2 + 1] = 0;
				}
			}
			++p;
		}
		send(client_socket, buf, sizeof(BusHeader) + sizeof(BusPacket) * 50, 0);
	}
}
