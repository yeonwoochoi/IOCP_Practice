#include "ChatServer.h"

const UINT16 SERVER_PORT = 11021;
const UINT16 MAX_CLIENT = 100;		// 동시 접속 가능한 최대 클라이언트 수
const UINT32 MAX_IO_WORKER_THREAD = 4;

int main()
{
	ChatServer chatServer;

	chatServer.Init(MAX_IO_WORKER_THREAD);
	chatServer.BindandListen(SERVER_PORT);

	printf("채팅 서버 시작. 콘솔 입력 -> 전체 공지, 'quit' 입력 시 종료\n");

	// SendThread(콘솔 입력 루프)를 이 스레드에서 직접 실행 -> 'quit' 입력까지 블록
	chatServer.Run(MAX_CLIENT);

	chatServer.End();
	return 0;
}
