#include "ChatServer.h"

#include <iostream>
#include <string>
#include <chrono>
#include <cstring>
#include <cstdarg>

namespace {
	std::vector<char> MakePacket(const std::string& body) {
		UINT16 total = (UINT16)(PACKET_HEADER_SIZE + body.size());
		std::vector<char> buf(total);

		PacketHeader header;
		header.PacketSize = total;
		memcpy(buf.data(), &header, PACKET_HEADER_SIZE);
		if (body.empty() == false) {
			memcpy(buf.data() + PACKET_HEADER_SIZE, body.data(), body.size());
		}
		return buf;
	}
}

void ChatServer::Run(const UINT32 maxClient) {
	StartServer(maxClient);

	mIsRunRecvThread = true;
	mReceiveThread = std::thread([this]() { RecvThread(); });

	// SendThread(콘솔 입력)는 호출 스레드(main)에서 직접 실행 -> "quit" 입력까지 여기서 블록
	mIsRunSendThread = true;
	SendThread();
}

void ChatServer::End() {
	// SendThread는 main 스레드에서 돌다 "quit"로 이미 빠져나온 상태
	mIsRunRecvThread = false;
	if (mReceiveThread.joinable()) mReceiveThread.join();

	DestroyThread();
}

void ChatServer::OnConnect(const UINT32 clientIndex) {
	Print("[OnConnect] index %d\n", clientIndex);
}

void ChatServer::OnClose(const UINT32 clientIndex) {
	{
		std::lock_guard<std::mutex> lock(mRwLock);
		RemoveClientFromRoom(clientIndex); // 방 멤버 + mClientRoom에서 제거
	}
	Print("[OnClose] index %d\n", clientIndex);
}

void ChatServer::OnReceive(const UINT32 clientIndex, const UINT32 gen, const UINT32 size, char* pData) {
	std::lock_guard<std::mutex> lock(mQueueMutex);
	mRecvPacketQueue.emplace_back(clientIndex, gen, size, pData);
}

void ChatServer::RecvThread() {
	while (mIsRunRecvThread) {
		std::deque<PacketData> local;
		{
			std::lock_guard<std::mutex> lock(mQueueMutex);
			local.swap(mRecvPacketQueue);
		}

		if (local.empty()) {
			std::this_thread::sleep_for(std::chrono::milliseconds(1));
			continue;
		}

		while (local.empty() == false) {
			PacketData packet = std::move(local.front());
			local.pop_front();

			if (packet.DataSize <= PACKET_HEADER_SIZE) continue;

			// 헤더 떼고 본문만 추출
			std::string text(packet.pPacketData + PACKET_HEADER_SIZE, packet.DataSize - PACKET_HEADER_SIZE);
			HandleText(packet.SessionIndex, packet.Generation, text);
		}
	}
}

void ChatServer::HandleText(UINT32 idx, UINT32 gen, const std::string& text) {
	ParsedCommand cmd = mCommandManager.Parse(text);

	if (cmd.isCommand == false) {
		// 일반 채팅 -> 같은 방 멤버에게 릴레이. 방에 없으면 경고
		if (BroadCast(idx, text.data(), (UINT32)text.size()) == false) {
			SendText(idx, gen, "you are not in a room. use /create or /join");
		}
		return;
	}

	if (cmd.name == "create") {
		if (cmd.args.empty()) { SendText(idx, gen, "usage: /create [RoomName]"); return; }
		UINT32 roomId = CreateRoom(idx, gen, cmd.args[0]);
		SendText(idx, gen, "created room #" + std::to_string(roomId) + " (" + cmd.args[0] + ")");
	}
	else if (cmd.name == "join") {
		if (cmd.args.empty()) { SendText(idx, gen, "usage: /join [RoomName]"); return; }
		UINT32 roomId;
		{
			std::lock_guard<std::mutex> lock(mRwLock);
			roomId = GetRoomIdByName(cmd.args[0]);
		}
		if (roomId == (UINT32)-1) { SendText(idx, gen, "no such room: " + cmd.args[0]); return; }
		if (JoinRoom(idx, gen, roomId)) SendText(idx, gen, "joined room #" + std::to_string(roomId));
		else SendText(idx, gen, "join failed");
	}
	else if (cmd.name == "leave") {
		if (LeaveRoom(idx)) SendText(idx, gen, "left room");
		else SendText(idx, gen, "you are not in a room");
	}
	else {
		SendText(idx, gen, "unknown command: /" + cmd.name);
	}
}

UINT32 ChatServer::CreateRoom(UINT32 clientIndex, UINT32 gen, const std::string& name) {
	std::lock_guard<std::mutex> lock(mRwLock);
	RemoveClientFromRoom(clientIndex); // 기존 방 떠나기

	UINT32 roomId = GenerateRoomId();
	auto room = std::make_unique<Room>();
	room->id = roomId;
	room->name = name;
	room->members.push_back({ clientIndex, gen });

	mRooms[roomId] = std::move(room);
	mClientRoom[clientIndex] = roomId;
	return roomId;
}

bool ChatServer::JoinRoom(UINT32 clientIndex, UINT32 gen, UINT32 roomId) {
	std::lock_guard<std::mutex> lock(mRwLock);
	RemoveClientFromRoom(clientIndex); // 기존 방 떠나기 (먼저)

	Room* room = GetRoom(roomId);
	if (room == nullptr) return false;

	room->members.push_back({ clientIndex, gen });
	mClientRoom[clientIndex] = roomId;
	return true;
}

bool ChatServer::LeaveRoom(UINT32 clientIndex) {
	std::lock_guard<std::mutex> lock(mRwLock);
	if (mClientRoom.find(clientIndex) == mClientRoom.end()) return false;
	RemoveClientFromRoom(clientIndex);
	return true;
}

bool ChatServer::BroadCast(UINT32 fromIndex, const char* data, UINT32 size) {
	std::vector<RoomMember> targets;
	{
		std::lock_guard<std::mutex> lock(mRwLock);
		Room* room = GetRoomByClient(fromIndex);
		if (room == nullptr) return false; // 방에 안 들어가 있으면 드롭
		targets = room->members; // 락 안에서 스냅샷만
	}

	std::string body = "[" + std::to_string(fromIndex) + "] " + std::string(data, size);
	auto pkt = MakePacket(body);

	for (const auto& m : targets) {
		SendMsg(m.index, m.gen, (UINT32)pkt.size(), pkt.data());
	}
	return true;
}

void ChatServer::SendText(UINT32 idx, UINT32 gen, const std::string& body) {
	auto pkt = MakePacket(body);
	SendMsg(idx, gen, (UINT32)pkt.size(), pkt.data());
}

Room* ChatServer::GetRoom(UINT32 roomId) {
	auto it = mRooms.find(roomId);
	return (it != mRooms.end()) ? it->second.get() : nullptr;
}

UINT32 ChatServer::GetRoomIdByName(const std::string& roomName) {
	for (const auto& [roomId, pRoom] : mRooms) {
		if (pRoom->name == roomName) return roomId;
	}
	return (UINT32)-1;
}

Room* ChatServer::GetRoomByClient(UINT32 clientIndex) {
	auto it = mClientRoom.find(clientIndex);
	if (it == mClientRoom.end()) return nullptr;
	return GetRoom(it->second);
}

void ChatServer::RemoveClientFromRoom(UINT32 clientIndex) {
	auto it = mClientRoom.find(clientIndex);
	if (it == mClientRoom.end()) return;

	Room* room = GetRoom(it->second);
	if (room != nullptr) room->RemoveMember(clientIndex);
	mClientRoom.erase(it);
}

UINT32 ChatServer::GenerateRoomId() {
	return mNextRoomId++;
}

void ChatServer::Print(const char* format, ...) {
	std::lock_guard<std::mutex> lock(mPrintLock);
	va_list args;
	va_start(args, format);
	vprintf(format, args);
	va_end(args);
}

void ChatServer::SendThread() {
	std::string line;
	while (mIsRunSendThread) {
		if (!std::getline(std::cin, line)) break; // EOF면 종료
		if (line == "quit") {
			mIsRunSendThread = false;
			break;
		}

		std::string body = "[SERVER] " + line;
		auto pkt = MakePacket(body);
		SendToAll((UINT32)pkt.size(), pkt.data()); // 접속 중인 모두에게
	}
}
