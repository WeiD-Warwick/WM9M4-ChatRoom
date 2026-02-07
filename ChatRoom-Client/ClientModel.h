#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include <unordered_map>

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


struct ChatMsg { 
    bool fromMe;
    std::string text;
    Chatter sender;
};

struct PrivateChatWindow {
    Chatter user;
    bool open = true;
    std::string inputBuffer;
    std::vector<ChatMsg> messages;
};

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
    Chatter me;


    bool openLogin = false;
    bool openMainChat = false;
    bool openPrivateChat = false;

    std::vector<PrivateChatWindow> privateChats;

    std::vector<ChatMsg> mainChatMessages;

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

    void openPrivateChatFor(const Chatter& user) {
        for (auto& chat : privateChats) {
            if (chat.user.chatterID == user.chatterID) {
                chat.open = true;
                chat.inputBuffer.clear();
                chat.messages.clear();
                return;
            }
        }
        PrivateChatWindow window{};
        window.user = user;
        privateChats.push_back(std::move(window));
    }

    void removeClosedPrivateChats() {
        for (size_t i = 0; i < privateChats.size(); ) {
            if (!privateChats[i].open) {
                privateChats.erase(privateChats.begin() + static_cast<long long>(i));
            }
            else {
                ++i;
            }
        }
    }
};