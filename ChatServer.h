#pragma once

#include "Server/IOCPServer.h"
#include "Packet.h"
#include "CommandManager.h"

#include <vector>
#include <deque>
#include <thread>
#include <mutex>
#include <unordered_map>

struct RoomMember { UINT32 index; UINT32 gen; };
struct Room {
	UINT32 id;
	std::string name;
	std::vector<RoomMember> members;
};

class ChatServer : public IOCPServer {
public:
	ChatServer() = default;
	virtual ~ChatServer() = default;

	virtual void OnConnect(const UINT32 clientIndex) override;
	virtual void OnClose(const UINT32 clientIndex) override;
	virtual void OnReceive(const UINT32 clientIndex, const UINT32 gen, const UINT32 size, char* pData) override;

private:
	void Print(char* data, const UINT32 length) {
		printf("%.*s", length, data);
	}

	void Print(char* data) {
		printf("%s", data);
	}

	void SendThread();
	void RecvThread();

	void Run();
	void End();

	UINT32 CreateRoom(UINT32 clientIndex, UINT32 gen, const std::string& name); // room id 반환
	bool JoinRoom(UINT32 clientIndex, UINT32 gen, UINT32 roomId);
	bool LeaveRoom(UINT32 clientIndex);

	bool BroadCast(UINT32 fromIndex, const char* data, UINT32 size);
	Room* GetRoom(UINT32 roomIndex);
	UINT32 GenerateRoomId();
	void HandleText(UINT32 idx, UINT32 gen, const std::string& text); // 명령어 파싱

	std::atomic<bool> mIsRunRecvThread = false;
	std::atomic<bool> mIsRunSendThread = false;
	UINT32 mNextRoomId = 1;

	std::mutex mRwLock; // mRooms, mClientRoom 보호
	std::mutex mPrintLock;
	std::deque<PacketData> mRecvPacketQueue;
	std::thread mSendThread; // getline으로 입력 대기 -> 입력하면 파싱 -> 명령어에 맞는 액션 dispatch
	std::thread mReceiveThread; // 패킷 데이터 출력

	std::unordered_map<UINT32, std::unique_ptr<Room>> mRooms;
	std::unordered_map<UINT32, UINT32> mClientRoom; // clientIndex -> roomId

	CommandManager mCommandManager;
};