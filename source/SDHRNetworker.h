#pragma once

#pragma pack(push)
#pragma pack(1)

/**
 * @brief Bus packet struct simulating the data going through the Apple 2 bus
*/

struct BusPacket {
	BYTE seqno[4];
	BYTE cmdtype;
	BYTE rwflags;
	BYTE seqflags;
	BYTE data[8];
	BYTE addrs[16];
};

#pragma pack(pop)

enum SDHRCtrl_e
{
	SDHR_CTRL_DISABLE = 0,
	SDHR_CTRL_ENABLE,
	SDHR_CTRL_PROCESS,
	SDHR_CTRL_RESET
};

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
	SDHRNetworker();
	~SDHRNetworker();
	bool IsEnabled();
	bool Connect();
	bool Connect(std::string server_ip, int server_port);
	void Disconnect();
	bool IsConnected() { return bIsConnected; };
	void BusCtrlPacket(BYTE command);
	void BusDataPacket(BYTE data);
	void BusMemoryPacket(BYTE data, WORD addr);
	void BusDataCommandStream(BYTE* data, int length);
	void BusDataMemoryStream(LPBYTE memPtr, WORD source_address, int length);

private:
	BusPacket s_packet = { 0 };
	SOCKET client_socket = NULL;
	sockaddr_in server_addr = { 0 };
	bool bIsConnected = false;
	uint32_t next_seqno = 0;
};

