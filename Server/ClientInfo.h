#pragma once

#include "Define.h"
#include <stdio.h>
#include <mutex>
#include <queue>

class stClientInfo {
public:
	stClientInfo() {
		ZeroMemory(&mRecvOverlappedEx, sizeof(stOverlappedEx));
		mSocket = INVALID_SOCKET;
	}

	void Init(UINT32 sessionIndex, HANDLE iocpHandle) {
		mIndex = sessionIndex;
		mIOCPHandle = iocpHandle;
	}

	UINT32 GetIndex() { return mIndex; }
	bool IsConnectd() { return mIsConnect == 1; }
	bool IsReusable() { return mIsConnect == 0 && mClosing == false && mIORefCount == 0; } // 재사용 조건 (다시 accept 걸어도 되는지)
	SOCKET GetSocket() { return mSocket; }
	char* GetRecvBuffer() { return mRecvBuf + mWritePos; }
	INT32 GetRemainBufferSize() { return MAX_SOCK_RECVBUF - mWritePos; }
	void AddRecvData(const UINT32 size) { mWritePos += size; }
	UINT32 GetGeneration() { return mGeneration; }
	void Clear() {}

	char* GetPacket(UINT32* outPacketSize) {
		INT32 readable = mWritePos - mReadPos;
		if (readable < (INT32)PACKET_HEADER_SIZE) {
			return nullptr;
		}

		PacketHeader* header = (PacketHeader*)(mRecvBuf + mReadPos);
		if (header->PacketSize < PACKET_HEADER_SIZE) {
			return nullptr;
		}
		if (readable < header->PacketSize) {
			return nullptr;
		}

		char* packet = mRecvBuf + mReadPos;
		*outPacketSize = header->PacketSize;
		mReadPos += header->PacketSize;
		return packet;
	}

	void CompactRecvBuffer() {
		INT32 remain = mWritePos - mReadPos;
		if (remain > 0 && mReadPos > 0) {
			memmove(mRecvBuf, mRecvBuf + mReadPos, remain);
		}
		mWritePos = remain;
		mReadPos = 0;
	}

	// 커널에 Accept 예약 던지고 리턴 (AcceptorThread)
	bool PostAccept(SOCKET listenSock) {
		++mGeneration;
		printf_s("Accept. client Index: %d\n", GetIndex());
		mListenSocket = listenSock;

		// AcceptEx는 미리 빈 소켓 만들어 줘야함.
		// Accept되면 커널에서 이 소켓에 accept된 소켓 연결해줌
		mSocket = WSASocket(AF_INET, SOCK_STREAM, IPPROTO_TCP, NULL, 0, WSA_FLAG_OVERLAPPED);
		if (mSocket == INVALID_SOCKET) {
			printf_s("client Socket WSASocket Error : %d\n", GetLastError());
			return false;
		}

		// mAcceptContext 초기화
		ZeroMemory(&mAcceptOverlappedEx, sizeof(stOverlappedEx));
		mAcceptOverlappedEx.m_wsaBuf.len = 0;
		mAcceptOverlappedEx.m_wsaBuf.buf = nullptr;
		mAcceptOverlappedEx.SessionIndex = mIndex;
		mAcceptOverlappedEx.m_eOperation = IOOperation::ACCEPT;

		DWORD bytes = 0;
		DWORD flags = 0;
		
		AddRef();
		// 커널에 accept 예약 후 리턴 (비동기, 논블로킹) -> 완료되면 mAcceptOverlappedEx 통해 IOCP 통지 -> 워커 스레드에서 (GetQueuedCompletionStatus로 받아 처리)
		if (AcceptEx(listenSock, mSocket, mAcceptBuf, 0, sizeof(SOCKADDR_IN) + 16, sizeof(SOCKADDR_IN) + 16, &bytes, (LPWSAOVERLAPPED)&mAcceptOverlappedEx) == FALSE) {
			if (WSAGetLastError() != WSA_IO_PENDING) {
				printf_s("AcceptEx Error : %d\n", GetLastError());
				ReleaseRef();
				return false;
			}
		}
		return true;
	}

	// Accept 실제로 완료 되었을때 후처리 (WorkerThread)
	bool OnAcceptCompleted() {
		// 원래 Accept 하면 listenSock의 속성 다 상속 받음
		// AcceptEx는 자동 상속 x -> SO_UPDATE_ACCEPT_CONTEXT 호출해서 직접 컨텍스트 갱신 해줘야함.
		printf_s("OnAcceptCompleted : SessionIndex(%d)\n", mIndex);
		Clear();

		// 1. IOCP에 클라이언트 소켓 등록
		if (BindIOCompletionPort(mIOCPHandle) == false) {
			printf_s("[Session %d] BindIOCompletionPort Failed! Error: %d\n", mIndex, GetLastError());
			return false;
		}

		// 2. listensocket 옵션 상속 (SO_UPDATE_ACCEPT_CONTEXT)
		setsockopt(mSocket, SOL_SOCKET, SO_UPDATE_ACCEPT_CONTEXT, (char*)&mListenSocket, sizeof(mListenSocket)); // 상속

		// 3. 상대 주소 조회 (이건 뭐 굳이 안해도 되긴 함. 걍 로그 띄우려고)
		SOCKADDR_IN	stClientAddr{};
		int nAddrLen = sizeof(stClientAddr);
		getpeername(mSocket, (SOCKADDR*)&stClientAddr, &nAddrLen);
		char clientIP[32] = { 0 };
		inet_ntop(AF_INET, &(stClientAddr.sin_addr), clientIP, sizeof(clientIP));

		printf("Client Connected : IP(%s) SOCKET(%d)\n", clientIP, (int)mSocket);

		// 4. 최초 Recv 예약
		if (PostRecv() == false) {
			printf_s("[Session %d] PostRecv Failed! Error: %d\n", mIndex, WSAGetLastError());
			return false;
		}

		mIsConnect = 1;
		return true;
	}

	// Accept 후 IOCP에 mSocket 등록 -> 그래야 IO 완료 통지 받지 (GQCS)
	bool BindIOCompletionPort(HANDLE iocpHandle) {
		HANDLE hIOCP = CreateIoCompletionPort((HANDLE)mSocket, iocpHandle, (ULONG_PTR)this, 0);
		if (hIOCP == NULL || hIOCP != iocpHandle) {
			printf("CreateIoCompletionPort failed: %d", GetLastError());
			return false;
		}

		return true;
	}

	bool PostRecv() {
		DWORD dwFlag = 0;
		DWORD dwRecvNumBytes = 0;

		mRecvOverlappedEx.m_wsaBuf.buf = GetRecvBuffer();
		mRecvOverlappedEx.m_wsaBuf.len = GetRemainBufferSize();
		mRecvOverlappedEx.m_eOperation = IOOperation::RECV;

		AddRef();
		int nRet = WSARecv(mSocket, &mRecvOverlappedEx.m_wsaBuf, 1, &dwRecvNumBytes, &dwFlag, (LPWSAOVERLAPPED)&mRecvOverlappedEx, NULL);
		if (nRet == SOCKET_ERROR && WSAGetLastError() != ERROR_IO_PENDING) {
			printf("WSARecv failed: %d\n", WSAGetLastError());
			ReleaseRef();
			return false;
		}
		return true;
	}
	
	bool SendMsg(const UINT32 dataSize, char* pMsg, UINT32 expectedGen) {
		std::lock_guard<std::mutex> guard(mSendLock);
		if (mGeneration != expectedGen || mSocket == INVALID_SOCKET) {
			return false;
		}

		auto sendOverlappedEx = new stOverlappedEx();
		ZeroMemory(sendOverlappedEx, sizeof(stOverlappedEx));
		sendOverlappedEx->m_wsaBuf.len = dataSize;
		sendOverlappedEx->m_wsaBuf.buf = new char[dataSize];
		sendOverlappedEx->m_sendBuf = sendOverlappedEx->m_wsaBuf.buf;
		CopyMemory(sendOverlappedEx->m_wsaBuf.buf, pMsg, dataSize);
		sendOverlappedEx->m_eOperation = IOOperation::SEND;

		mSendDataQueue.push(sendOverlappedEx);
		// 소켓당 WSASend는 한 번에 하나만 (in-flight) 처리하기 위해 이렇게 처리.
		if (mSendDataQueue.size() == 1) {
			if (SendIO() == false) {
				printf_s("[Session %d] SendMsg - SendIO Failed! Error: %d, Size: %llu\n", mIndex, WSAGetLastError(), mSendDataQueue.size());
			}
		}

		return true;
	}

	void OnSendCompleted(const UINT32 dataSize) {
		printf("WSASend Completed (bytes : %d)\n", dataSize);
		std::lock_guard<std::mutex> guard(mSendLock);

		auto ctx = mSendDataQueue.front();

		// 부분 송신
		if (dataSize < ctx->m_wsaBuf.len) {
			// 보낸만큼 앞으로 다시 밀고 재전송
			ctx->m_wsaBuf.buf += dataSize;
			ctx->m_wsaBuf.len -= dataSize;
			if (SendIO() == false) {
				printf_s("[Session %d] OnSendCompleted - resend Failed! Error: %d\n", mIndex, WSAGetLastError());
			}
			return; // send queue에서 pop하면 안됨 (다 전송되고 pop해야함)
		}


		delete[] ctx->m_sendBuf;
		delete ctx;
		mSendDataQueue.pop();

		if (mSendDataQueue.empty() == false) {
			if (SendIO() == false) {
				printf_s("[Session %d] OnSendCompleted - SendIO Failed! Error: %d, Remaining: %llu\n", mIndex, WSAGetLastError(), mSendDataQueue.size());
			}
		}
	}

	// 소켓 Close 로직만 (자원 delete하는 로직은 따로 분리)
	// -> 모든 io가 종료된 이후 FinalizeClose()에서 자원 정리할거임.
	void Close(bool isForce = false) {
		if (mSocket == INVALID_SOCKET)
			return;

		mClosing = true;

		// Linger(0, 0)일 때 (기본값): closesocket 호출했을때 송신 버퍼에 남아있는 패킷을 다보내고 FIN 보냄
		// Linger(1, 5)일 때: 송신 버퍼에 남은 데이터가 다 갈 때까지 closesocket() 함수가 리턴하지 않고 최대 5초 동안 스레드를 붙잡고 기다림
		// Linger(1, 0)일 때: 송신 버퍼 데이터를 버리고 상대에게 RST(강제종료) 패킷을 날려 TIME_WAIT 없이 소켓을 즉시 소멸
		linger stLinger = { 0, 0 };
		if (isForce == true)
			stLinger.l_onoff = 1;

		shutdown(mSocket, SD_BOTH);
		setsockopt(mSocket, SOL_SOCKET, SO_LINGER, (char*)&stLinger, sizeof(stLinger));

		closesocket(mSocket);
		mSocket = INVALID_SOCKET;
	}

	void AddRef() { ++mIORefCount; }

	// refCount가 0이 됐는데 mClosing==false면 -> 정상 동작 중에 잠깐 0된 거니까 정리하면 안 됨
	// refCount가 0이 됐고 mClosing == true면 -> 닫으라고 했고 이제 마지막 I/O까지 다 빠졌으니 이때만 정리
	// 반환값 true = 소켓 닫히고 마지막 io 작업도 마무리된거니 호출자에서 OnClose 호출하면됨.
	bool ReleaseRef() {
		long v = --mIORefCount;
		if (v == 0 && mClosing) {
			bool wasConnected = (mIsConnect == 1);
			FinalizeClose();
			return wasConnected;
		}
		return false;
	}

private:
	bool SendIO() {
		auto sendOverlappedEx = mSendDataQueue.front();
		DWORD dwSendNumBytes = 0;

		AddRef();
		int nRet = WSASend(mSocket, &sendOverlappedEx->m_wsaBuf, 1, &dwSendNumBytes, 0, (LPWSAOVERLAPPED)&sendOverlappedEx->m_wsaOverlapped, NULL);
		if (nRet == SOCKET_ERROR && WSAGetLastError() != ERROR_IO_PENDING) {
			printf("[에러] WSASend()함수 실패 : %d\n", WSAGetLastError());
			ReleaseRef();
			return false;
		}

		return true;
	}

	// refCount 0 도달 후 떠 있는 I/O가 없을 때만 호출됨 (자원 정리 책임만)
	void FinalizeClose() {
		std::lock_guard<std::mutex> guard(mSendLock);
		while (mSendDataQueue.empty() == false) {
			delete[] mSendDataQueue.front()->m_sendBuf;
			delete mSendDataQueue.front();
			mSendDataQueue.pop();
		}
		mIsConnect = 0;
		mClosing = false; // 풀어줘야 다시 AccepterThread에서 재사용 가능
	}

	std::atomic<UINT32> mGeneration = 0;
	INT32 mIndex = 0;
	HANDLE mIOCPHandle = INVALID_HANDLE_VALUE;

	std::atomic<INT64> mIsConnect = 0; // accept, worker 스레드 둘다 사용해서 atomic 처리
	std::atomic<long> mIORefCount = 0; // 지금 커널에 떠 있는 비동기 I/O 개수 (in-flight)
	std::atomic<bool> mClosing = false; // refCount가 0일 때 걍 io 작업이 임시로 없는건지 닫혀서 0인건지 구분 위한 인자

	SOCKET mListenSocket = INVALID_SOCKET;
	SOCKET mSocket = INVALID_SOCKET;

	stOverlappedEx mAcceptOverlappedEx;
	char mAcceptBuf[64]; // 로컬 주소 정보 (sizeof(sockaddr_in) + 16 = 32) + 원격 주소 정보 (sizeof(sockaddr_in) + 16 = 32)

	stOverlappedEx mRecvOverlappedEx;
	char mRecvBuf[MAX_SOCK_RECVBUF];
	INT32 mReadPos = 0;
	INT32 mWritePos = 0;

	std::mutex mSendLock;
	std::queue<stOverlappedEx*> mSendDataQueue; // front만 in-flight(전송중)이 보장됨.
};