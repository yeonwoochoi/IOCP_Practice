#pragma once

#include "Server/IOCPServer.h"
#include "Packet.h"

#include <vector>
#include <deque>
#include <thread>
#include <mutex>

class EchoServer : public IOCPServer {
public:
	EchoServer() = default;
	virtual ~EchoServer() = default;

	virtual void OnConnect(const UINT32 clientIndex) override;
	virtual void OnClose(const UINT32 clientIndex) override;
	virtual void OnReceive(const UINT32 clientIndex, const UINT32 gen, const UINT32 size, char* pData) override;

	void Run(const UINT32 maxClient);
	void End();

private:
	void ProcessPacket();
	PacketData DequePacketData();

	std::atomic<bool> mIsRunProcessThread = false;

	std::mutex mLock;
	std::deque<PacketData> mPacketDataQueue;
	std::thread mProcessThread;
};
