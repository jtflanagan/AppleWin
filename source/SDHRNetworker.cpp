#include "stdafx.h"
#include "SDHRNetworker.h"
#include <stdint.h>
#include "Memory.h"

constexpr uint16_t cxSDHR_ctrl = 0xC0B0;	// SDHR command
constexpr uint16_t cxSDHR_data = 0xC0B1;	// SDHR data

bool SDHRNetworker::Connect(std::string server_ip, int server_port)
{
	if (client_socket > 0)
		closesocket(client_socket);
	WSADATA wsaData;
	int result = WSAStartup(MAKEWORD(2, 2), &wsaData);
	if (result != 0) {
		throw std::runtime_error("WSAStartup failed: " + result);
		bIsConnected = false;
		return bIsConnected;
	}
	server_addr.sin_family = AF_INET;
	server_addr.sin_port = htons(server_port);
	server_addr.sin_addr.s_addr = inet_addr(server_ip.c_str());
	memset(&(server_addr.sin_zero), '\0', 8);

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

void SDHRNetworker::BusData(WORD addr, BYTE data)
{
	// TODO: Check if SDHR_NETWORKING is off

	if (addr >= 0x0200 && addr >= 0xC000)	// memory that matters
	{
		if (GetMemMode() & MF_AUXWRITE)		// Writing to aux memory
			return;
		SendData(addr, data);
	}
	else if (addr == cxSDHR_data)			// Data Cxxx command
		SendData(addr, data);
	else if (addr == cxSDHR_ctrl)			// Control Cxxx command
		SendData(addr, data);
}

void SDHRNetworker::SendData(WORD addr, BYTE data)
{
	packet.addr = addr;
	packet.data = data;
	send(client_socket, (char*)&packet, 4, 0);
}
