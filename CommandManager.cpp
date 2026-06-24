#include "CommandManager.h"
#include <sstream>

// "/create RoomA" -> { isCommand=true, name="create", args=["RoomA"] }
// "안녕하세요"      -> { isCommand=false }
ParsedCommand CommandManager::Parse(const std::string& rawText) {
	ParsedCommand result;
	if (rawText.empty() || rawText[0] != '/') {
		return result; // 일반 메시지 (isCommand=false)
	}

	result.isCommand = true;
	std::istringstream iss(rawText.substr(1)); // 맨 앞 '/' 제거
	std::string token;
	if (iss >> token) {
		result.name = token; // 첫 토큰 = 명령 이름
		while (iss >> token) {
			result.args.push_back(token); // 나머지 = 인자
		}
	}
	return result;
}
