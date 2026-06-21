#pragma once

#include <winsock2.h>
#include <WS2tcpip.h>
#include <mswsock.h>

#pragma pack(push, 1) // 패딩 없이 정확히 2byte로 맞추려고
struct PacketHeader {
	UINT16 PacketSize;
};
#pragma pack(pop)

const UINT32 MAX_SOCK_RECVBUF = 8192;	// 소켓 버퍼의 크기
const UINT32 MAX_SOCK_SENDBUF = 4096;	// 소켓 버퍼의 크기
const UINT32 PACKET_HEADER_SIZE = sizeof(PacketHeader);

enum class IOOperation {
	ACCEPT,
	RECV,
	SEND
};

struct stOverlappedEx {
	WSAOVERLAPPED m_wsaOverlapped;
	WSABUF		m_wsaBuf;
	IOOperation m_eOperation;
	UINT32 SessionIndex = 0;
	char* m_sendBuf = nullptr; // 원본 send 포인터 (부분 send할때 원본 잃으니까)
};