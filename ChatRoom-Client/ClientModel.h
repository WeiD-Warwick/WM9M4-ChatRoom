#pragma once
#include <cstdint>
#include <string>

#include "Model.h"

using ClientEvent = struct {
    enum class Type { 
        Connected,
        IncomingMsg,
        Disconnected,
        Error
    } type{};
    Message msg{};
    int err{};
};
