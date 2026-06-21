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

	virtual void OnConnect(const UINT32 clientIndex) override {
		printf("[OnConnect] 클라이언트: Index(%d)\n", clientIndex);
	}

	virtual void OnClose(const UINT32 clientIndex) override {
		printf("[OnClose] 클라이언트: Index(%d)\n", clientIndex);
	}

	virtual void OnReceive(const UINT32 clientIndex, const UINT32 gen, const UINT32 size, char* pData) override {
		printf("[OnReceive] 클라이언트: Index(%d), dataSize(%d)\n", clientIndex, size);
		std::lock_guard<std::mutex> guard(mLock);
		mPacketDataQueue.emplace_back(clientIndex, gen, size, pData);
	}

	void Run(const UINT32 maxClient) {
		mIsRunProcessThread = true;
		mProcessThread = std::thread([this]() { ProcessPacket(); });
		StartServer(maxClient);
	}

	void End() {
		mIsRunProcessThread = false;
		if (mProcessThread.joinable())
			mProcessThread.join();

		DestroyThread();
	}

private:
	void ProcessPacket() {
		while (mIsRunProcessThread) {
			auto packetData = DequePacketData();
			if (packetData.DataSize != 0) {
				SendMsg(packetData.SessionIndex, packetData.Generation, packetData.DataSize, packetData.pPacketData);
			}
			else {
				std::this_thread::sleep_for(std::chrono::milliseconds(1));
			}
		}
	}

	PacketData DequePacketData() {
		PacketData packetData;

		std::lock_guard<std::mutex> guard(mLock);
		if (mPacketDataQueue.empty()) {
			return PacketData();
		}

		packetData = std::move(mPacketDataQueue.front());
		mPacketDataQueue.pop_front();
		return packetData;
	}


	bool mIsRunProcessThread = false;

	std::mutex mLock;
	std::deque<PacketData> mPacketDataQueue;
	std::thread mProcessThread;
};