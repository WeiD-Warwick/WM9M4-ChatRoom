#pragma once
#include <cstdint>
#include <memory>   // ? for std::shared_ptr
#include <string>

#include "Model.h"

using SessionId = uint64_t;

class ServerSession;

struct ServerEvent {
    enum class Type {
        Connected,      // new connection
        IncomingMsg,    // recv a full message
        Disconnected    // recv error
    } type{};

    SessionId sessionID{};
    std::shared_ptr<ServerSession> session;
    Message msg{};
};
