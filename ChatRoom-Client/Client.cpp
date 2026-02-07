#include "ClientSession.h"
#include "ClientModel.h"
#include "Model.h"
#include "ThreadSafeQueue.h"
#include <winsock2.h>
#include <ws2tcpip.h>
#include <atomic>
#include <iostream>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <unordered_map>

#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxguid.lib")

namespace {
    struct ClientState {
        std::mutex mu;
        std::unordered_map<std::string, std::string> idToName;
        std::optional<Chatter> self;
    };

    void printHelp() {
        std::cout << "Commands:\n"
            << "  /help                Show this help\n"
            << "  /list                List known online users\n"
            << "  /w <id> <message>     Send private message to user id\n"
            << "  /quit                Quit client\n"
            << "  (default)             Send group message\n";
    }

    void printUserList(const ClientState& state) {
        std::cout << "Online users:\n";
        if (state.idToName.empty()) {
            std::cout << "  (none)\n";
            return;
        }
        for (const auto& [id, name] : state.idToName) {
            std::cout << "  " << name << " (" << id << ")\n";
        }
    }

    void handleIncoming(const Message& msg, ClientState& state) {
        switch (msg.type) {
        case MessageType::Welcome: {
            Chatter me = Chatter::decode(msg.body);
            {
                std::lock_guard<std::mutex> lock(state.mu);
                state.self = me;
                state.idToName[me.chatterID] = me.chatterName;
            }
            std::cout << "[System] Welcome " << me.chatterName << " (id=" << me.chatterID << ")\n";
            break;
        }
        case MessageType::ChatGroup: {
            GroupChat gc = GroupChat::decode(msg.body);
            std::cout << "[Group] " << gc.sender.chatterName << " (" << gc.sender.chatterID
                << "): " << gc.content << "\n";
            break;
        }
        case MessageType::ChatPrivate: {
            PrivateChat pc = PrivateChat::decode(msg.body);
            std::cout << "[Private] " << pc.sender.chatterName << " (" << pc.sender.chatterID
                << ") -> " << pc.receiver.chatterName << " (" << pc.receiver.chatterID
                << "): " << pc.content << "\n";
            break;
        }
        case MessageType::UserJoin: {
            UserJoin join = UserJoin::decode(msg.body);
            {
                std::lock_guard<std::mutex> lock(state.mu);
                state.idToName[join.user.chatterID] = join.user.chatterName;
            }
            std::cout << "[System] " << join.user.chatterName << " (" << join.user.chatterID << ")"
                << join.text << "\n";
            break;
        }
        case MessageType::UserLeave: {
            UserLeave leave = UserLeave::decode(msg.body);
            {
                std::lock_guard<std::mutex> lock(state.mu);
                state.idToName.erase(leave.user.chatterID);
            }
            std::cout << "[System] " << leave.user.chatterName << " (" << leave.user.chatterID << ")"
                << leave.text << "\n";
            break;
        }
        case MessageType::SystemMessage: {
            SystemMessage systemMessage = SystemMessage::decode(msg.body);
            std::cout << "[System] " << systemMessage.text << "\n";
            break;
        }
        default:
            std::cout << "[System] Unsupported message type: " << static_cast<int>(msg.type) << "\n";
            break;
        }
    }
}

int main(int argc, char** argv) {
    std::string host = "127.0.0.1";
    int port = 65432;
    if (argc >= 2) {
        host = argv[1];
    }
    if (argc >= 3) {
        port = std::stoi(argv[2]);
    }

    std::cout << "Enter your name: ";
    std::string name;
    std::getline(std::cin, name);
    if (name.empty()) {
        std::cout << "Name cannot be empty.\n";
        return 1;
    }

    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        std::cout << "WSAStartup failed.\n";
        return 1;
    }

    SOCKET sock = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sock == INVALID_SOCKET) {
        std::cout << "Socket creation failed.\n";
        WSACleanup();
        return 1;
    }

    sockaddr_in serverAddr{};
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(static_cast<u_short>(port));
    if (inet_pton(AF_INET, host.c_str(), &serverAddr.sin_addr) <= 0) {
        std::cout << "Invalid server address.\n";
        ::closesocket(sock);
        WSACleanup();
        return 1;
    }

    if (::connect(sock, reinterpret_cast<sockaddr*>(&serverAddr), sizeof(serverAddr)) == SOCKET_ERROR) {
        std::cout << "Connect failed.\n";
        ::closesocket(sock);
        WSACleanup();
        return 1;
    }

    ThreadSafeQueue<ClientEvent> queue;
    ClientSession session(sock, &queue);
    session.start();

    Message hello{ MessageType::Hello, HelloMsg{ name }.encode() };
    session.sendMessage(hello);

    ClientState state;
    std::atomic<bool> running{ true };

    std::thread eventThread([&] {
        ClientEvent event;
        while (running.load() && queue.wait_pop(event)) {
            switch (event.type) {
            case ClientEvent::Type::IncomingMsg:
                handleIncoming(event.msg, state);
                break;
            case ClientEvent::Type::Disconnected:
                std::cout << "[System] Disconnected from server.\n";
                running.store(false);
                break;
            case ClientEvent::Type::Error:
                std::cout << "[System] Socket error: " << event.err << "\n";
                running.store(false);
                break;
            default:
                break;
            }
        }
        });

    printHelp();
    while (running.load()) {
        std::string line;
        if (!std::getline(std::cin, line)) {
            break;
        }
        if (line.empty()) {
            continue;
        }
        if (line == "/quit") {
            running.store(false);
            break;
        }
        if (line == "/help") {
            printHelp();
            continue;
        }
        if (line == "/list") {
            std::lock_guard<std::mutex> lock(state.mu);
            printUserList(state);
            continue;
        }
        if (line.rfind("/w ", 0) == 0) {
            auto rest = line.substr(3);
            auto pos = rest.find(' ');
            if (pos == std::string::npos) {
                std::cout << "Usage: /w <id> <message>\n";
                continue;
            }
            std::string id = rest.substr(0, pos);
            std::string content = rest.substr(pos + 1);
            if (content.empty()) {
                std::cout << "Message cannot be empty.\n";
                continue;
            }
            Chatter receiver(id, "");
            PrivateChat pc{ Chatter("", ""), receiver, content };
            Message msg{ MessageType::ChatPrivate, pc.encode() };
            session.sendMessage(msg);
            continue;
        }

        GroupChat gc{ Chatter("", ""), line };
        Message msg{ MessageType::ChatGroup, gc.encode() };
        session.sendMessage(msg);
    }

    queue.stop();
    session.stop();
    if (eventThread.joinable()) {
        eventThread.join();
    }
    WSACleanup();
    return 0;
}