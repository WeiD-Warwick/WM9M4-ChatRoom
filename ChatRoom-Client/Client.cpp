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
            Log(std::format("[Client] Welcome received. id={} name={}", me.chatterID, me.chatterName));
            break;
        }
        case MessageType::ChatGroup: {
            GroupChat gc = GroupChat::decode(msg.body);
            Log(std::format("[Client] Group message received. from={}({})", gc.sender.chatterName, gc.sender.chatterID));
            break;
        }
        case MessageType::ChatPrivate: {
            PrivateChat pc = PrivateChat::decode(msg.body);
            Log(std::format("[Client] Private message received. from={}({}) to={}({})", pc.sender.chatterName, pc.sender.chatterID, pc.receiver.chatterName, pc.receiver.chatterID));
            break;
        }
        case MessageType::UserJoin: {
            UserJoin join = UserJoin::decode(msg.body);
            {
                std::lock_guard<std::mutex> lock(state.mu);
                state.idToName[join.user.chatterID] = join.user.chatterName;
            }
            Log(std::format("[Client] User joined. id={} name={}", join.user.chatterID, join.user.chatterName));
            break;
        }
        case MessageType::UserLeave: {
            UserLeave leave = UserLeave::decode(msg.body);
            {
                std::lock_guard<std::mutex> lock(state.mu);
                state.idToName.erase(leave.user.chatterID);
            }
            Log(std::format("[Client] User left. id={} name={}", leave.user.chatterID, leave.user.chatterName));
            break;
        }
        case MessageType::SystemMessage: {
            SystemMessage systemMessage = SystemMessage::decode(msg.body);
            Log(std::format("[Client] System message received. text={}", systemMessage.text));
            break;
        }
        default:
            Log(std::format("[Client] Unsupported message type received: {}", static_cast<int>(msg.type)));
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

    Log(std::format("[Client] Starting. target={}:{}", host, port));
    std::cout << "Enter your name: ";
    std::string name;
    std::getline(std::cin, name);
    if (name.empty()) {
        std::cout << "Name cannot be empty.\n";
        return 1;
    }

    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        Log("[Client] WSAStartup failed.");
        return 1;
    }

    SOCKET sock = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sock == INVALID_SOCKET) {
        Log("[Client] Socket creation failed.");
        WSACleanup();
        return 1;
    }

    sockaddr_in serverAddr{};
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(static_cast<u_short>(port));
    if (inet_pton(AF_INET, host.c_str(), &serverAddr.sin_addr) <= 0) {
        Log(std::format("[Client] Invalid server address: {}", host));
        ::closesocket(sock);
        WSACleanup();
        return 1;
    }

    if (::connect(sock, reinterpret_cast<sockaddr*>(&serverAddr), sizeof(serverAddr)) == SOCKET_ERROR) {
        Log(std::format("[Client] Connect failed. target={}:{}", host, port));
        ::closesocket(sock);
        WSACleanup();
        return 1;
    }

    Log(std::format("[Client] Connected. target={}:{}", host, port));
    ThreadSafeQueue<ClientEvent> queue;
    ClientSession session(sock, &queue);
    session.start();

    Message hello{ MessageType::Hello, HelloMsg{ name }.encode() };
    session.sendMessage(hello);

    Log(std::format("[Client] Sent hello. name={}", name));

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
                Log("[Client] Disconnected from server.");
                running.store(false);
                break;
            case ClientEvent::Type::Error:
                Log(std::format("[Client] Socket error: {}", event.err));
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
            Log(std::format("[Client] Sent private message to {}.", id));
            continue;
        }

        GroupChat gc{ Chatter("", ""), line };
        Message msg{ MessageType::ChatGroup, gc.encode() };
        session.sendMessage(msg);
        Log("[Client] Sent group message.");
    }

    queue.stop();
    session.stop();
    if (eventThread.joinable()) {
        eventThread.join();
    }
    WSACleanup();
    return 0;
}