#pragma once

#define SDHR_STREAM_CHUNK  64	// Packets to send at a time in a Bus Data Stream call 

#pragma pack(push)
#pragma pack(1)

/**
 * @brief Bus packet struct simulating the data going through the Apple 2 bus
*/

struct BusPacket {
	WORD addr;
	BYTE data;
	BYTE pad;
};

/**
 * @brief SDHR_STREAM_CHUNK-byte block used to send command data over the network. This is purely
 * for efficency and avoiding sending packets one at a time when the commands are big
*/

struct BusStream {
	BusPacket packets[SDHR_STREAM_CHUNK];
};

#pragma pack(pop)

/**
 * @brief Class that sends Apple 2 bus packets to the network
 * It only sends packets that matter to the SDHR server on the other side:
 * - Any Apple 2 memory block sent via BusDataMemoryStream
 * - Any packet to address 0xC080 : SDHR Command via BusCtrlPacket
 * - Any packet to address 0xC081 : SDHR data via BusDataPacket
 * - Any number of packets to address 0xC081: SDHR data in bulk via BusDataCommandStream
*/

class SDHRNetworker
{
public:
	bool IsEnabled();
	bool Connect();
	bool Connect(std::string server_ip, int server_port);
	bool IsConnected() { return bIsConnected; };
	void BusCtrlPacket(BYTE command);
	void BusDataPacket(BYTE data);
	void BusDataCommandStream(BYTE* data, int length);
	void BusDataMemoryStream(LPBYTE memPtr, WORD source_address, int length);

private:
	BusPacket s_packet = { 0 };
	BusStream s_stream = { 0 };
	SOCKET client_socket = NULL;
	sockaddr_in server_addr = { 0 };
	bool bIsEnabled = false;
	bool bIsConnected = false;

	void Disconnect();
};

