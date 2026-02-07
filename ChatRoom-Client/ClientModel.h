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

struct ChatMsg { bool fromMe; std::string text; };

class ChatModel {
public:

    enum class LoginType {
        Default,
        Connected,
        Connecting,
        NetError,
        Disconnected,
        EmptyName
    };


    bool isConnected = false;
    std::string nameBuffer;
    std::string inputBuffer;
    LoginType state = LoginType::Default;


    bool openLogin = false;
    bool openMainChat = false;
    bool openPrivateChat = false;

    inline static ChatMsg demoMsgs[] = {
        { false, "Hello!" },
        { true,  "Hi, I'm here." },
        { false, "How are you?" },
        { true,  "Good. Let's test bubble layout." },
    };


    std::string statusMessage() {
        switch (state) {
        case LoginType::Default: return "";
        case LoginType::Connected: return "Connection Successful!";
        case LoginType::Connecting: return "Connecting...";
        case LoginType::NetError: return "Network Error.";
        case LoginType::Disconnected: return "Server Disconnected.";
        case LoginType::EmptyName: return "You Need A NickName";
        default: return "";
        }
    }
};