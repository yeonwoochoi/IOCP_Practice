#pragma once
#pragma comment(lib, "ws2_32")
#pragma comment(lib, "mswsock.lib")
#include "Define.h"
#include "ClientInfo.h"

#include <thread>
#include <vector>
#include <atomic>

class IOCPServer {
public:
	IOCPServer(void) {}

	virtual ~IOCPServer(void) {
		WSACleanup();
	}

	virtual void OnConnect(const UINT32 clientIndex) {};
	virtual void OnClose(const UINT32 clientIndex) {};
	virtual void OnReceive(const UINT32 clientIndex, const UINT32 gen, const UINT32 size, char* pData) {}

	bool Init(const UINT32 maxIOWorkerThreadCount);
	bool BindandListen(int nBindPort);
	bool StartServer(const UINT32 maxClientCount);
	void DestroyThread();

	bool SendMsg(const UINT32 sessionIndex, const UINT32 gen, const UINT32 dataSize, char* pData);
	bool SendToAll(const UINT32 dataSize, char* pData);

private:
	void CreateClient(const UINT32 maxClientCount);
	bool CreateWorkerThread();
	bool CreateAccepterThread();

	stClientInfo* GetEmptyClientInfo();
	stClientInfo* GetClientInfo(const UINT32 sessionIndex);

	void WorkerThread();
	void AccepterThread();
	void DisconnectClient(stClientInfo* pClientInfo, bool bIsForce = false);

	std::vector<stClientInfo*> mClientInfos;
	SOCKET				mListenSocket = INVALID_SOCKET;
	std::atomic<int>	mClientCnt = 0;
	std::vector<std::thread> mIOWorkerThreads;
	std::thread			mAccepterThread;
	HANDLE				mIOCPHandle = INVALID_HANDLE_VALUE;
	std::atomic<bool>	mIsWorkerRun = true;
	std::atomic<bool>	mIsAccepterRun = true;

	UINT32 mMaxIOWorkerThreadCount = 0;
};
