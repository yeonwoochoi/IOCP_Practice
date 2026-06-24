#pragma once

#include "Server/IOCPServer.h"
#include "Packet.h"
#include "CommandManager.h"

#include <vector>
#include <deque>
#include <thread>
#include <mutex>
#include <memory>
#include <algorithm>
#include <unordered_map>

struct RoomMember { UINT32 index; UINT32 gen; };

struct Room {
	UINT32 id;
	std::string name;
	std::vector<RoomMember> members;

	void RemoveMember(UINT32 index) {
		auto it = std::find_if(members.begin(), members.end(), [index](const RoomMember& member) {
			return member.index == index;
		});
		if (it != members.end()) {
			*it = members.back(); // 뒤 원소로 덮고 pop (순서 무관 빠른 제거)
			members.pop_back();
		}
	}
};

class ChatServer : public IOCPServer {
public:
	ChatServer() = default;
	virtual ~ChatServer() = default;

	virtual void OnConnect(const UINT32 clientIndex) override;
	virtual void OnClose(const UINT32 clientIndex) override;
	virtual void OnReceive(const UINT32 clientIndex, const UINT32 gen, const UINT32 size, char* pData) override;

	void Run(const UINT32 maxClient);
	void End();

private:
	void Print(const char* format, ...); // printf 스타일 (mPrintLock으로 직렬화)

	void SendThread(); // 콘솔 입력 대기 -> 전체 broadcast
	void RecvThread(); // 수신 큐 처리 -> 명령/채팅 dispatch

	UINT32 CreateRoom(UINT32 clientIndex, UINT32 gen, const std::string& name); // room id 반환
	bool JoinRoom(UINT32 clientIndex, UINT32 gen, UINT32 roomId);
	bool LeaveRoom(UINT32 clientIndex);

	bool BroadCast(UINT32 fromIndex, const char* data, UINT32 size); // 같은 방에 뿌리기
	void SendText(UINT32 idx, UINT32 gen, const std::string& body);  // 한 클라에게 (프레이밍해서)
	void HandleText(UINT32 idx, UINT32 gen, const std::string& text);

	// 아래 helper들은 호출자가 mRwLock을 쥔 상태에서만 호출 (자체적으로 락 안 잡음)
	Room* GetRoom(UINT32 roomId);
	UINT32 GetRoomIdByName(const std::string& roomName);
	Room* GetRoomByClient(UINT32 clientIndex);
	UINT32 GenerateRoomId();
	void RemoveClientFromRoom(UINT32 clientIndex); // 기존 방에서 빼기

	std::atomic<bool> mIsRunRecvThread = false;
	std::atomic<bool> mIsRunSendThread = false;
	UINT32 mNextRoomId = 1;

	std::mutex mRwLock;     // mRooms, mClientRoom 보호
	std::mutex mPrintLock;  // 콘솔 출력 직렬화
	std::mutex mQueueMutex; // mRecvPacketQueue 보호
	std::deque<PacketData> mRecvPacketQueue;
	std::thread mReceiveThread;

	std::unordered_map<UINT32, std::unique_ptr<Room>> mRooms; // roomId -> Room
	std::unordered_map<UINT32, UINT32> mClientRoom;           // clientIndex -> roomId

	CommandManager mCommandManager;
};
