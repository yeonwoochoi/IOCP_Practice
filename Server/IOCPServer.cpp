#include "IOCPServer.h"

bool IOCPServer::Init(const UINT32 maxIOWorkerThreadCount) {
	WSAData wsaData;
	int nRet = WSAStartup(MAKEWORD(2, 2), &wsaData);
	if (nRet != 0) {
		printf("WSAStartup Failed: %d\n", WSAGetLastError());
		return false;
	}

	mListenSocket = WSASocket(AF_INET, SOCK_STREAM, IPPROTO_TCP, NULL, NULL, WSA_FLAG_OVERLAPPED);
	if (mListenSocket == INVALID_SOCKET) {
		printf("WSASocket Failed : %d\n", WSAGetLastError());
		return false;
	}

	mMaxIOWorkerThreadCount = maxIOWorkerThreadCount;

	printf("Init Socket Success\n");
	return true;
}

bool IOCPServer::BindandListen(int nBindPort) {
	SOCKADDR_IN		stServerAddr;
	stServerAddr.sin_family = AF_INET;
	stServerAddr.sin_port = htons(nBindPort);
	stServerAddr.sin_addr.s_addr = htonl(INADDR_ANY);

	int nRet = bind(mListenSocket, (SOCKADDR*)&stServerAddr, sizeof(SOCKADDR_IN));
	if (nRet != 0) {
		printf("bind failed: %d\n", WSAGetLastError());
		return false;
	}

	nRet = listen(mListenSocket, SOMAXCONN); // 대기 큐 최대치는 SOMAXCONN (5)
	if (nRet != 0) {
		printf("listen failed: %d\n", WSAGetLastError());
		return false;
	}

	// IOCP 객체 생성
	mIOCPHandle = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, NULL, mMaxIOWorkerThreadCount);
	if (mIOCPHandle == NULL) {
		printf("CreateIoCompletionPort failed: %d\n", WSAGetLastError());
		return false;
	}

	// mListenSocket을 IOCP 객체에 바인딩 (AcceptEx 사용하니까)
	auto hIOCPHandle = CreateIoCompletionPort((HANDLE)mListenSocket, mIOCPHandle, (UINT32)0, 0);
	if (hIOCPHandle == nullptr) {
		printf("listen socket IOCP bind failed : %d\n", WSAGetLastError());
		return false;
	}

	printf("Bind & Listen Success\n");
	return true;
}

bool IOCPServer::StartServer(const UINT32 maxClientCount) {
	CreateClient(maxClientCount);

	bool bRet = CreateWorkerThread();
	if (bRet == false) {
		printf("CreateWorkerThread failed: %d\n", WSAGetLastError());
		return false;
	}

	bRet = CreateAccepterThread();
	if (bRet == false) {
		printf("CreateAccepterThread failed: %d\n", WSAGetLastError());
		return false;
	}

	printf("Start Server\n");
	return true;
}

void IOCPServer::DestroyThread() {
	// 워커 스레드 개수만큼 종료 신호를 넣어서 자고있는 worker thread 모두 깨우기..
	mIsWorkerRun = false;
	for (size_t i = 0; i < mIOWorkerThreads.size(); ++i) {
		PostQueuedCompletionStatus(mIOCPHandle, 0, 0, NULL);
	}
	for (auto& th : mIOWorkerThreads)
		if (th.joinable()) th.join();

	mIsAccepterRun = false;
	if (mAccepterThread.joinable())
	mAccepterThread.join();

	closesocket(mListenSocket);
	CloseHandle(mIOCPHandle); // 워커스레드 다 빠진 뒤에 닫기
}

bool IOCPServer::SendMsg(const UINT32 sessionIndex, const UINT32 gen, const UINT32 dataSize, char* pData) {
	auto pClient = GetClientInfo(sessionIndex);
	return pClient->SendMsg(dataSize, pData, gen);
}

bool IOCPServer::SendToAll(const UINT32 dataSize, char* pData)
{
	for (auto client: mClientInfos){
		if (client == nullptr || client->IsConnectd() == false) {
			continue;
		}

		client->SendMsg(dataSize, pData, client->GetGeneration());
	}

	return true;
}

void IOCPServer::CreateClient(const UINT32 maxClientCount) {
	for (UINT32 i = 0; i < maxClientCount; ++i) {
		auto client = new stClientInfo();
		client->Init(i, mIOCPHandle);
		mClientInfos.push_back(client);
	}
}

bool IOCPServer::CreateWorkerThread() {
	for (UINT32 i = 0; i < mMaxIOWorkerThreadCount; ++i) {
		mIOWorkerThreads.emplace_back([this]() { WorkerThread(); });
	}
	printf("WorkerThread Start\n");
	return true;
}

bool IOCPServer::CreateAccepterThread() {
	mAccepterThread = std::thread([this]() { AccepterThread(); });
	printf("Accept Start\n");
	return true;
}

stClientInfo* IOCPServer::GetEmptyClientInfo() {
	for (auto& client : mClientInfos) {
		if (client->IsConnectd() == false) {
			return client;
		}
	}
	return nullptr;
}

stClientInfo* IOCPServer::GetClientInfo(const UINT32 sessionIndex) {
	return mClientInfos[sessionIndex];
}

void IOCPServer::WorkerThread() {
	stClientInfo* pClientInfo = NULL;	// CompletionKey로 넘어온 세션 포인터
	BOOL bSuccess = TRUE;				// I/O 성공 여부
	DWORD dwIoSize = 0;					// 이번 Overlapped I/O로 전송된 바이트 수
	LPOVERLAPPED lpOverlapped = NULL;	// 완료된 작업의 OVERLAPPED 포인터

	while (mIsWorkerRun) {
		bSuccess = GetQueuedCompletionStatus(
			mIOCPHandle,
			&dwIoSize,					// 전송된 바이트 수
			(PULONG_PTR)&pClientInfo,	// CompletionKey (등록 때 넘긴 stClientInfo* 가 그대로 돌아옴)
			&lpOverlapped,				// 완료된 OVERLAPPED 구조체
			INFINITE					// 완료될 때까지 무한 대기
		);

		// PostQueuedCompletionStatus(0,0,NULL) 받아서 빠져나가는 코드
		if (bSuccess == TRUE && dwIoSize == 0 && lpOverlapped == NULL) {
			mIsWorkerRun = false;
			continue;
		}

		if (lpOverlapped == NULL) {
			continue;
		}

		stOverlappedEx* pOverlappedEx = (stOverlappedEx*)lpOverlapped;

		// closesocket(mListenSocket) -> 걸려있던 AcceptEx 모두 실패로 완료 -> pClientInfo == NULL 이기 떄문에 방어 코드 (복원)
		if (pOverlappedEx->m_eOperation == IOOperation::ACCEPT) {
			pClientInfo = GetClientInfo(pOverlappedEx->SessionIndex);
		}

		// 클라이언트 연결이 끊겼거나 I/O 에러가 발생한 경우
		bool bIsFailure = (bSuccess == FALSE || (dwIoSize == 0 && pOverlappedEx->m_eOperation != IOOperation::ACCEPT));
		if (bIsFailure) {
			DisconnectClient(pClientInfo);
		}
		else if (pOverlappedEx->m_eOperation == IOOperation::ACCEPT) {
			if (pClientInfo->OnAcceptCompleted()) {
				++mClientCnt;
				OnConnect(pClientInfo->GetIndex());
			}
			else {
				DisconnectClient(pClientInfo, true);
			}
		}
		else if (pOverlappedEx->m_eOperation == IOOperation::RECV) {
			pClientInfo->AddRecvData(dwIoSize);

			// 완전한 패킷 다 뺴낼 때까지 돌아
			auto onRecv = [this, pClientInfo](UINT32 size, char* pData) {
				this->OnReceive(pClientInfo->GetIndex(), pClientInfo->GetGeneration(), size, pData);
			};

			// ParsePacket 함수에 람다를 전달합니다.
			if (pClientInfo->ParsePacket(onRecv) == false) {
				// 패킷 파싱 실패 혹은 연결 종료 처리
				DisconnectClient(pClientInfo);
			}

			if (pClientInfo->PostRecv() == false) {
				DisconnectClient(pClientInfo);
			}
		}
		else if (pOverlappedEx->m_eOperation == IOOperation::SEND) {
			pClientInfo->OnSendCompleted(dwIoSize);
		}
		else {
			printf("Client Index(%d) Unknown IO Operation\n", pClientInfo->GetIndex());
		}

		// 마지막 I/0도 완료(refCount == 0)되었고 socket은 닫힌 상태
		if (pClientInfo->ReleaseRef()) {
			--mClientCnt;
			OnClose(pClientInfo->GetIndex());
		}
	}
}

void IOCPServer::AccepterThread() {
	while (mIsAccepterRun) {
		for (auto client : mClientInfos) {
			if (client->IsReusable() == false) {
				continue;
			}

			client->PostAccept(mListenSocket);
		}

		std::this_thread::sleep_for(std::chrono::milliseconds(32));
	}
}

void IOCPServer::DisconnectClient(stClientInfo* pClientInfo, bool bIsForce) {
	pClientInfo->Close(bIsForce);
}
