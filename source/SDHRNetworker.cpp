#include "stdafx.h"
#include "SDHRNetworker.h"
#include <stdint.h>
#include "Memory.h"
#include "Interface.h"
#include "Configuration/IPropertySheet.h"
#include "Registry.h"

constexpr uint16_t cxSDHR_ctrl = 0xC0B0;	// SDHR command
constexpr uint16_t cxSDHR_data = 0xC0B1;	// SDHR data

bool SDHRNetworker::Connect()
{
	return SDHRNetworker::Connect("", 0);
}

bool SDHRNetworker::Connect(std::string server_ip, int server_port)
{
	if (client_socket > 0)
		closesocket(client_socket);

	DWORD _sdhrIsEnabled = 0;
	REGLOAD_DEFAULT(TEXT(REGVALUE_SDHR_REMOTE_ENABLED), &_sdhrIsEnabled, 0);
	if (_sdhrIsEnabled == 0)
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

	client_socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (client_socket == INVALID_SOCKET) {
		throw std::runtime_error("Error creating socket: " + WSAGetLastError());
		WSACleanup();
		bIsConnected = false;
		return bIsConnected;
	}
	if (connect(client_socket, (sockaddr*)&server_addr, sizeof(server_addr)) == SOCKET_ERROR) {
		throw std::runtime_error("Error connecting to server: " + WSAGetLastError());
		closesocket(client_socket);
		WSACleanup();
		bIsConnected = false;
		return bIsConnected;
	}
	bIsConnected = true;

	packet.pad = 0;
	return bIsConnected;
}

void SDHRNetworker::BusDataPacket(WORD addr, BYTE data)
{
	if (!bIsConnected)
		return;
	if (addr == cxSDHR_data || addr == cxSDHR_ctrl)
	{
		packet.addr = addr;
		packet.data = data;
		send(client_socket, (char*)&packet, 4, 0);
	}
}

void SDHRNetworker::BusDataBlock(WORD start_addr, bool isMainMemory)
{
	LPBYTE memPtr;
	isMainMemory ? memPtr = MemGetMainPtr(start_addr) : memPtr = MemGetAuxPtr(start_addr);
	for (size_t i = 0; i < SDHR_UPLOAD_SIZE; i++)
	{
		block.packets[i].addr = start_addr + i;
		block.packets[i].data = *(memPtr + i);
	}
	send(client_socket, (char*)&block, SDHR_UPLOAD_SIZE, 0);
}
