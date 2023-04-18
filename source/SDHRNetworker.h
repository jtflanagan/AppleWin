#pragma once

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

#pragma pop

/**
 * @brief Class that sends Apple 2 bus packets to the network
 * It only sends packets that matter to the SDHR server on the other side:
 * - Any MAIN memory packet between 0x0200 and 0xBFFF
 * - Any packet to address 0xC080 : SDHR Command
 * - Any packet to address 0xC081 : SDHR data
*/

class SDHRNetworker
{
public:
	bool Connect(std::string server_ip, int server_port);
	bool IsConnected() { return bIsConnected; };
	void BusData(WORD addr, BYTE data);

private:
	BusPacket packet = { 0 };
	SOCKET client_socket = NULL;
	sockaddr_in server_addr = { 0 };
	bool bIsConnected = false;

	void SendData(WORD addr, BYTE data);
};

