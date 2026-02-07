#pragma once
#include "ClientSession.h"
#include "ClientModel.h"
#include "Model.h"
#include "ThreadSafeQueue.h"
#include <winsock2.h>
#include <ws2tcpip.h>
#include <atomic>
#include <format>
#include <iostream>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <unordered_map>
#include "AudioPlayer.h"

namespace {

    void printHelp() {
        std::cout << "Commands:\n"
            << "  /help                Show this help\n"
            << "  /list                List known online users\n"
            << "  /w <id> <message>     Send private message to user id\n"
            << "  /quit                Quit client\n"
            << "  (default)             Send group message\n";
    }
}

class Client {
public:
    Client(std::string host, int port) : _host(std::move(host)), _port(port) {
        player.init();
    }

    bool start(const std::string& userName) {
        Log(std::format("{:<{}} Starting. target={}:{}", "[Main Thread]", tag_w, _host, _port));

        WSADATA wsaData;
        if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
            Log(std::format("{:<{}} WSAStartup failed.", "[Main Thread]", tag_w));
            return false;
        }

        _clientSocket = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (_clientSocket == INVALID_SOCKET) {
            Log(std::format("{:<{}} Socket creation failed.", "[Main Thread]", tag_w));
            WSACleanup();
            return false;
        }

        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons((u_short)_port);

        if (inet_pton(AF_INET, _host.c_str(), &addr.sin_addr) <= 0) {
            Log(std::format("{:<{}} Invalid server address: {}", "[Main Thread]", tag_w, _host));
            ::closesocket(_clientSocket);
            _clientSocket = INVALID_SOCKET;
            WSACleanup();
            return false;
        }

        if (::connect(_clientSocket, (sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR) {
            Log(std::format("{:<{}} Connect failed. target={}:{}", "[Main Thread]", tag_w, _host, _port));
            ::closesocket(_clientSocket);
            _clientSocket = INVALID_SOCKET;
            WSACleanup();
            return false;
        }

        Log(std::format("{:<{}} Connected. target={}:{}", "[Main Thread]", tag_w, _host, _port));

        _session = std::make_unique<ClientSession>(_clientSocket, &_queue);
        _session->start();

        Message hello{ MessageType::Hello, HelloMsg{ userName }.encode() };
        _session->sendMessage(hello);

        Log(std::format("{:<{}} Sent HELLO. name={}", "[Main Thread]", tag_w, userName));

        _running.store(true);
        _eventThread = std::thread([this] { eventLoop(); });
        return true;
    }

    void stop() {
        _running.store(false);
        _queue.stop();

        if (_session) {
            _session->stop();
            _session.reset();
            _clientSocket = INVALID_SOCKET;
        }

        if (_eventThread.joinable()) {
            _eventThread.join();
        }

        if (_clientSocket != INVALID_SOCKET) {
            ::closesocket(_clientSocket);
            _clientSocket = INVALID_SOCKET;
        }

        WSACleanup();
    }


    bool isRunning() const {
        return _running.load();
    }

    void printUserList() {
        std::cout << "Online users:\n";
        std::lock_guard<std::mutex> lock(_stateMu);
        if (_idToName.empty()) {
            std::cout << "  (none)\n";
            return;
        }
        for (const auto& [id, name] : _idToName) {
            std::cout << "  " << name << " (" << id << ")\n";
        }
    }

    void sendGroupMessage(const std::string& text) {
        if (text.empty() || !_session) {
            return;
        }
        GroupChat gc{ currentUserOrEmpty(), text };
        Message msg{ MessageType::ChatGroup, gc.encode() };
        _session->sendMessage(msg);
        Log("[Client] Sent group message.");
    }

    void sendPrivateMessage(const std::string& receiverId, const std::string& text) {
        if (receiverId.empty() || text.empty() || !_session) {
            return;
        }
        Chatter receiver(receiverId, "default");
        PrivateChat pc{ currentUserOrEmpty(), receiver, text };
        Message msg{ MessageType::ChatPrivate, pc.encode() };
        _session->sendMessage(msg);
        Log(std::format("[Client] Sent private message to {}.", receiverId));
    }

    std::vector<Chatter> getCurrentOnlineUser() {
        std::vector<Chatter> result;
        result.reserve(_idToName.size());
        std::lock_guard<std::mutex> lock(_stateMu);
        for (const auto& [id, name] : _idToName) {
            result.emplace_back(id, name);
        }
        return result;
    }

    void processIncoming(ChatModel& model) {
        Message msg;
        while (_incomingQueue.try_pop(msg)) {
            handleIncoming(msg, model);
        }
        player.update();
    }

private:
    void eventLoop() {
        ClientEvent event;
        while (_running.load() && _queue.wait_pop(event)) {
            switch (event.type) {
            case ClientEvent::Type::IncomingMsg:
                _incomingQueue.push(event.msg);
                break;
            case ClientEvent::Type::Disconnected:
                Log("[Client] Disconnected from server.");
                _running.store(false);
                break;
            case ClientEvent::Type::Error:
                Log(std::format("[Client] Socket error: {}", event.err));
                _running.store(false);
                break;
            default:
                break;
            }
        }
    }

    void handleIncoming(const Message& msg, ChatModel& model) {
        switch (msg.type) {
        case MessageType::Welcome: {
            WelcomeMsg welcome = WelcomeMsg::decode(msg.body);
            {
                std::lock_guard<std::mutex> lock(_stateMu);
                _self = welcome.me;
                _idToName[welcome.me.chatterID] = welcome.me.chatterName;

                for (const auto& user : welcome.allUsers) {
                    _idToName[user.chatterID] = user.chatterName;
                }
            }
            model.me = welcome.me;
            Log(std::format("[Client] Welcome. ID: {}, Synced {} users.", welcome.me.chatterID, welcome.allUsers.size()));
            break;
        }
        case MessageType::ChatGroup: {
            GroupChat gc = GroupChat::decode(msg.body);
            ChatMsg chatMsg;
            chatMsg.fromMe = (gc.sender.chatterID == model.me.chatterID);
            chatMsg.text = gc.content;
            chatMsg.sender = gc.sender;
            chatMsg.type = ChatMsg::Type::Normal;
            model.mainChatMessages.push_back(std::move(chatMsg));

            if (gc.sender.chatterID != _self.chatterID) {
                player.playAlert();
            }

            Log(std::format("[Client] Group message received. from={}({}). content={}", gc.sender.chatterName, gc.sender.chatterID, gc.content));
            break;
        }
        case MessageType::ChatPrivate: {
            PrivateChat pc = PrivateChat::decode(msg.body);
            const bool fromMe = (pc.sender.chatterID == model.me.chatterID);
            const Chatter& otherUser = fromMe ? pc.receiver : pc.sender;
            PrivateChatWindow* targetWindow = nullptr;
            for (auto& window : model.privateChats) {
                if (window.user.chatterID == otherUser.chatterID) {
                    window.open = true;
                    targetWindow = &window;
                    break;
                }
            }
            if (!targetWindow) {
                PrivateChatWindow window{};
                window.user = otherUser;
                model.privateChats.push_back(std::move(window));
                targetWindow = &model.privateChats.back();
            }
            ChatMsg chatMsg;
            chatMsg.fromMe = fromMe;
            chatMsg.text = pc.content;
            chatMsg.sender = pc.sender;
            chatMsg.type = ChatMsg::Type::Normal;
            targetWindow->messages.push_back(std::move(chatMsg));

            if (pc.sender.chatterID != _self.chatterID) {
                player.playAlert();
            }

            Log(std::format("[Client] Private message received. from={}({}) to={}({}). content={}", pc.sender.chatterName, pc.sender.chatterID, pc.receiver.chatterName, pc.receiver.chatterID, pc.content));
            break;
        }
        case MessageType::UserJoin: {
            UserJoin join = UserJoin::decode(msg.body);
            {
                std::lock_guard<std::mutex> lock(_stateMu);
                _idToName[join.user.chatterID] = join.user.chatterName;
            }

            ChatMsg chatMsg;
            chatMsg.fromMe = false;
            chatMsg.text = join.user.chatterName;
            chatMsg.sender = Chatter("default", "System");
            chatMsg.type = ChatMsg::Type::SystemJoin;
            model.mainChatMessages.push_back(std::move(chatMsg));

            if (join.user.chatterID != _self.chatterID) {
                player.playJoin();
            }

            Log(std::format("[Client] User joined. id={} name={}", join.user.chatterID, join.user.chatterName));
            break;
        }
        case MessageType::UserLeave: {
            UserLeave leave = UserLeave::decode(msg.body);
            {
                std::lock_guard<std::mutex> lock(_stateMu);
                _idToName.erase(leave.user.chatterID);
            }
            ChatMsg chatMsg;
            chatMsg.fromMe = false;
            chatMsg.text = leave.user.chatterName;
            chatMsg.sender = Chatter("default", "System");
            chatMsg.type = ChatMsg::Type::SystemLeave;
            model.mainChatMessages.push_back(std::move(chatMsg));
            Log(std::format("[Client] User left. id={} name={}", leave.user.chatterID, leave.user.chatterName));
            break;
        }
        case MessageType::SystemMessage: {
            SystemMessage systemMessage = SystemMessage::decode(msg.body);
            ChatMsg chatMsg;
            chatMsg.fromMe = false;
            chatMsg.text = systemMessage.text;
            chatMsg.sender = Chatter("default", "System");
            chatMsg.type = ChatMsg::Type::SystemNotice;
            model.mainChatMessages.push_back(std::move(chatMsg));
            Log(std::format("[Client] System message received. text={}", systemMessage.text));
            break;
        }
        default:
            Log(std::format("[Client] Unsupported message type received: {}", static_cast<int>(msg.type)));
            break;
        }
    }

    Chatter currentUserOrEmpty() {
        std::lock_guard<std::mutex> lock(_stateMu);
        if (!_self.isEmpty()) {
            return _self;
        }
        return Chatter("", "");
    }

    int _port{};
    std::string _host{};
    SOCKET _clientSocket{ INVALID_SOCKET };
    ThreadSafeQueue<ClientEvent> _queue;
    std::atomic<bool> _running{ false };
    std::thread _eventThread;
    std::unique_ptr<ClientSession> _session;
    ThreadSafeQueue<Message> _incomingQueue;

    std::mutex _stateMu;
    std::unordered_map<std::string, std::string> _idToName;
    Chatter _self;

    AudioPlayer player;

};
