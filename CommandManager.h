#pragma once

#include <vector>
#include <string>

struct ParsedCommand {
    bool isCommand = false;          // '/'∑Œ Ω√¿€?
    std::string name;                // "create", "join", "leave"
    std::vector<std::string> args;   // ["RoomName"]
};

class CommandManager {
public:
    ParsedCommand Parse(const std::string& rawText);
};
