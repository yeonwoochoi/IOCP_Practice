#include "EchoServer.h"

void EchoServer::OnConnect(const UINT32 clientIndex) {
	printf("[OnConnect] 클라이언트: Index(%d)\n", clientIndex);
}

void EchoServer::OnClose(const UINT32 clientIndex) {
	printf("[OnClose] 클라이언트: Index(%d)\n", clientIndex);
}

void EchoServer::OnReceive(const UINT32 clientIndex, const UINT32 gen, const UINT32 size, char* pData) {
	printf("[OnReceive] 클라이언트: Index(%d), dataSize(%d)\n", clientIndex, size);
	std::lock_guard<std::mutex> guard(mLock);
	mPacketDataQueue.emplace_back(clientIndex, gen, size, pData);
}

void EchoServer::Run(const UINT32 maxClient) {
	mIsRunProcessThread = true;
	mProcessThread = std::thread([this]() { ProcessPacket(); });
	StartServer(maxClient);
}

void EchoServer::End() {
	mIsRunProcessThread = false;
	if (mProcessThread.joinable())
		mProcessThread.join();

	DestroyThread();
}

void EchoServer::ProcessPacket() {
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

PacketData EchoServer::DequePacketData() {
	PacketData packetData;

	std::lock_guard<std::mutex> guard(mLock);
	if (mPacketDataQueue.empty()) {
		return PacketData();
	}

	packetData = std::move(mPacketDataQueue.front());
	mPacketDataQueue.pop_front();
	return packetData;
}
