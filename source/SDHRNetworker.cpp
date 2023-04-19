#include "stdafx.h"
#include "SDHRNetworker.h"
#include <stdint.h>
#include "Memory.h"
#include "Interface.h"
#include "Configuration/IPropertySheet.h"
#include "Registry.h"

constexpr uint16_t cxSDHR_ctrl = 0xC0B0;	// SDHR command
constexpr uint16_t cxSDHR_data = 0xC0B1;	// SDHR data

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

	s_packet.pad = 0;
	return bIsConnected;
}

void SDHRNetworker::BusCtrlPacket(BYTE command)
{
	s_packet.addr = cxSDHR_ctrl;
	s_packet.data = command;
	send(client_socket, (char*)&s_packet, 4, 0);
}

void SDHRNetworker::BusDataPacket(BYTE data)
{
	s_packet.addr = cxSDHR_data;
	s_packet.data = data;
	send(client_socket, (char*)&s_packet, 4, 0);
}

//////////////////////////////////////////////////////////////////////////
// Streams
//////////////////////////////////////////////////////////////////////////


void SDHRNetworker::BusDataCommandStream(BYTE* data, int length)
{
	// Send a bunch of SDHR_STREAM_CHUNK
	// and the remaining packets individually

	int numBlocks = length / SDHR_STREAM_CHUNK;
	for (size_t b = 0; b < numBlocks; b++)
	{
		for (size_t i = 0; i < SDHR_STREAM_CHUNK; i++)
		{
			s_stream.packets[i].addr = cxSDHR_data;
			s_stream.packets[i].data = *(data + i);
		}
		send(client_socket, (char*)&s_stream, sizeof(BusStream), 0);
		data += SDHR_STREAM_CHUNK;
	}
	int remainingPackets = length - (numBlocks * SDHR_STREAM_CHUNK);
	s_packet.addr = cxSDHR_data;
	for (size_t i = 0; i < remainingPackets; i++)
	{
		s_packet.data = *(data + i);
		send(client_socket, (char*)&s_packet, sizeof(BusPacket), 0);
	}
}

void SDHRNetworker::BusDataMemoryStream(LPBYTE memPtr, WORD source_address, int length)
{
	// Send a bunch of SDHR_STREAM_CHUNK
// and the remaining packets individually

	int numBlocks = length / SDHR_STREAM_CHUNK;
	int offset = 0;
	for (size_t b = 0; b < numBlocks; b++)
	{
		for (size_t i = 0; i < SDHR_STREAM_CHUNK; i++)
		{
			s_stream.packets[i].addr = source_address + offset;
			s_stream.packets[i].data = *(memPtr + offset);
			++offset;
		}
		send(client_socket, (char*)&s_stream, sizeof(BusPacket) * SDHR_STREAM_CHUNK, 0);
	}
	int remainingPackets = length - (numBlocks * SDHR_STREAM_CHUNK);
	for (size_t i = 0; i < remainingPackets; i++)
	{
		s_packet.addr = source_address + offset;;
		s_packet.data = *(memPtr + offset);
		++offset;
		send(client_socket, (char*)&s_packet, sizeof(BusPacket), 0);
	}
}
