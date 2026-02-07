#pragma once
#include <cstdint>
#include <string>

#include "Model.h"

struct ClientEvent {
    enum class Type { 
        Connected,
        IncomingMsg,
        Disconnected,
        Error
    } type{};
    Message msg{};
    int err{};
};
