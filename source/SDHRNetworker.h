#pragma once

#define SDHR_UPLOAD_SIZE 512

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
 * @brief 512-byte block used to upload data, simulating the SDHR graphics card
 * reading the Apple memory space
*/

struct BusBlock {
	BusPacket packets[SDHR_UPLOAD_SIZE];
};

#pragma pack(pop)

/**
 * @brief Class that sends Apple 2 bus packets to the network
 * It only sends packets that matter to the SDHR server on the other side:
 * - Any memory block of SDHR_UPLOAD_SIZE sent via BusDataBlock
 * - Any packet to address 0xC080 : SDHR Command
 * - Any packet to address 0xC081 : SDHR data
*/

class SDHRNetworker
{
public:
	bool Connect();
	bool Connect(std::string server_ip, int server_port);
	bool IsConnected() { return bIsConnected; };
	void BusDataPacket(WORD addr, BYTE data);
	void BusDataBlock(WORD start_addr, bool isMainMemory);

private:
	BusPacket packet = { 0 };
	BusBlock block = { 0 };
	SOCKET client_socket = NULL;
	sockaddr_in server_addr = { 0 };
	bool bIsConnected = false;
};

