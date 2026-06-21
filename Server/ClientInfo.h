#pragma once

#include "Define.h"
#include <stdio.h>
#include <mutex>
#include <queue>
#include <functional>
#include "../RingBuffer.h"

class stClientInfo {
public:
	stClientInfo() {
		ZeroMemory(&mRecvOverlappedEx, sizeof(stOverlappedEx));
		mSocket = INVALID_SOCKET;
	}

	~stClientInfo() {
		delete mRecvBuf;
	}

	void Init(UINT32 sessionIndex, HANDLE iocpHandle) {
		mIndex = sessionIndex;
		mIOCPHandle = iocpHandle;
		mRecvBuf = new RingBuffer(MAX_SOCK_RECVBUF);
	}

	// 1줄 접근자들은 헤더에 inline 유지
	UINT32 GetIndex() { return mIndex; }
	bool IsConnectd() { return mIsConnect == 1; }
	bool IsReusable() { return mIsConnect == 0 && mClosing == false && mIORefCount == 0; } // 재사용 조건 (다시 accept 걸어도 되는지)
	SOCKET GetSocket() { return mSocket; }
	char* GetRecvBuffer() { return (char*)mRecvBuf->GetWriteBuffer(); }
	INT32 GetRemainBufferSize() { return (INT32)(mRecvBuf->GetContinuousWritable()); }
	void AddRecvData(const UINT32 size) { mRecvBuf->Write(size); }
	UINT32 GetGeneration() { return mGeneration; }
	void Clear() { if (mRecvBuf) mRecvBuf->Clear(); } // 세션 재사용 시 수신버퍼 리셋
	void AddRef() { ++mIORefCount; }

	bool ParsePacket(std::function<void(UINT32, char*)> onRecv);

	bool PostAccept(SOCKET listenSock);
	bool OnAcceptCompleted();
	bool BindIOCompletionPort(HANDLE iocpHandle);
	bool PostRecv();

	bool SendMsg(const UINT32 dataSize, char* pMsg, UINT32 expectedGen);
	void OnSendCompleted(const UINT32 dataSize);

	void Close(bool isForce = false);
	bool ReleaseRef();

private:
	bool SendIO();
	void FinalizeClose();

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
	char mParseBuf[MAX_SOCK_RECVBUF];
	RingBuffer* mRecvBuf = nullptr;

	std::mutex mSendLock;
	std::queue<stOverlappedEx*> mSendDataQueue; // front만 in-flight(전송중)이 보장됨.
};
