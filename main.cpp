#include "EchoServer.h"
#include <string>
#include <iostream>

const UINT16 SERVER_PORT = 11021;
const UINT16 MAX_CLIENT = 100;		// 총 접속할수 있는 클라이언트 수
const UINT32 MAX_IO_WORKER_THREAD = 4;

int main()
{
	EchoServer echoServer;

	// 소켓을 초기화
	echoServer.Init(MAX_IO_WORKER_THREAD);

	// 소켓과 서버 주소를 연결하고 등록
	echoServer.BindandListen(SERVER_PORT);

	// Accept 스레드 + Worker 스레드 시작
	echoServer.Run(MAX_CLIENT);

	printf("아무 키나 누를 때까지 대기합니다\n");
	while (true) {
		std::string inputCmd;
		std::getline(std::cin, inputCmd);
		if (inputCmd == "quit") {
			break;
		}
	}

	echoServer.End();
	return 0;
}

