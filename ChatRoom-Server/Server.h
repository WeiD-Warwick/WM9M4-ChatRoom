#pragma once
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iostream>
#include <unordered_map>
#include <thread>
#include <atomic>

#include "ServerSession.h"
#include "ThreadSafeQueue.h"
#include "ServerModel.h"
#include "Model.h"

#pragma comment(lib, "ws2_32.lib")

class Server {
public:
    Server(int port) : _port(port) {}

    bool start() {
        WSADATA wsaData;
        if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) return false;

        _serverSocket = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (_serverSocket == INVALID_SOCKET) return false;

        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = INADDR_ANY;
        addr.sin_port = htons((u_short)_port);

        if (::bind(_serverSocket, (sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR) return false;
        if (::listen(_serverSocket, SOMAXCONN) == SOCKET_ERROR) return false;

        _running.store(true);

        // Accept Thread : accept new connection + create new session + post connection event
        _acceptThread = std::thread([this] { acceptLoop(); });

        // Event Thread : loop events + handle evnets 
        _eventThread = std::thread([this] { eventLoop(); });

        std::cout << "[Main Thread] Server started on port " << _port << "...\n";
        return true;
    }

    void stop() {
        _running.store(false);
        _queue.stop();

        if (_serverSocket != INVALID_SOCKET) {
            ::closesocket(_serverSocket);
            _serverSocket = INVALID_SOCKET;
        }

        if (_acceptThread.joinable()) _acceptThread.join();
        if (_eventThread.joinable()) _eventThread.join();

        // clear session
        for (auto& [sid, s] : _sessions) {
            s->stop();
        }
        _sessions.clear();
        WSACleanup();
    }

private:

    // accept client connection and create Session
    void acceptLoop() {
        std::cout << "[Accept Thread] id = " << std::this_thread::get_id() << std::endl;
        while (_running.load()) {
            SOCKET clientSock = ::accept(_serverSocket, nullptr, nullptr);
            if (clientSock == INVALID_SOCKET) continue;

            SessionId sid = _nextSid++;

            auto session = std::make_shared<ServerSession>(sid, clientSock, &_queue);
            // handle recv
            session->start();

            // push session to sessions to consume for eventThread
            ServerEvent event;
            event.type = ServerEvent::Type::Connected;
            event.sessionID = sid;
            event.session = session;
            _queue.push(std::move(event));

            std::cout << "[Accept Thread] A new client connected success. sid: " << sid << std::endl;
        }
    }

    void eventLoop() {
        std::cout << "[Event Thread] id = " << std::this_thread::get_id() << std::endl;
        ServerEvent event;
        while (_running.load() && _queue.wait_pop(event)) {
            switch (event.type) {
            case ServerEvent::Type::Connected:
                onConnected(event.sessionID, event.session);
                std::cout << "[Event Thread] New Client Connected. sid=" << event.sessionID << ", totalSession=" << _sessions.size() << std::endl;
                break;
            case ServerEvent::Type::IncomingMsg:
                onIncoming(event.sessionID, event.msg);
                std::cout << "[Event Thread] New Message Incoming. sid=" << event.sessionID << ", type: " << std::to_string((int)event.msg.type) << std::endl;
                break;
            case ServerEvent::Type::Disconnected:
                onDisconnected(event.sessionID);
                std::cout << "[Event Thread] Client Disconnected. sid: " << event.sessionID << ", remainingSession: " << _sessions.size() << std::endl;
                break;
            }
        }
    }

private:
    void onConnected(SessionId sid, const std::shared_ptr<ServerSession>& session) {
        _sessions[sid] = session;
    }

    void onDisconnected(SessionId sid) {
        auto it = _sessions.find(sid);
        if (it == _sessions.end()) return;

        // if sessionid belong to a login user, broadcast left
        if (it->second->user()) {
            SystemMessage ev{ *it->second->user(), " left." };
            Message m{ MessageType::UserLeave, ev.encode() };
            broadcast(m);
            // delete user
            _uidToSid.erase(it->second->user()->chatterID);
        }

        it->second->stop();
        _sessions.erase(it);
    }

    // Handle full message
    void onIncoming(SessionId sid, const Message& msg) {
        auto it = _sessions.find(sid);
        if (it == _sessions.end()) return;
        auto& session = it->second;

        switch (msg.type) {
        case MessageType::Hello: {
            HelloMsg hello = HelloMsg::decode(msg.body);
            // generate uid from server
            Chatter me(hello.name);
            session->setUser(me);
            _uidToSid[me.chatterID] = sid;

            // Send Welcom to client (attach uid)
            Message welcome { MessageType::Welcome, me.encode() };
            session->sendMessage(welcome);
            // broadcast to group that xxx join 
            UserJoin join { me, " joined." };
            Message joinMsg { MessageType::UserJoin, join.encode() };
            
            broadcast(joinMsg);
            break;
        }
        case MessageType::ChatGroup: {
            if (!session->user()) break;
            GroupChat gc = GroupChat::decode(msg.body);

            // sender base on session
            gc.sender = *session->user();

            Message out { MessageType::ChatGroup, gc.encode() };
            broadcast(out);
            break;
        }
        case MessageType::ChatPrivate: {
            if (!session->user()) break;
            PrivateChat pc = PrivateChat::decode(msg.body);

            // sender base on session
            pc.sender = *session->user();

            auto itSid = _uidToSid.find(pc.receiver.chatterID);
            if (itSid == _uidToSid.end()) {
                // offline
                SystemMessage event { *session->user(), "receiver not online" };
                Message systemMessage { MessageType::SystemMessage, event.encode() };

                // send system message to sender
                session->sendMessage(systemMessage);
                break;
            }

            auto itTarget = _sessions.find(itSid->second);
            if (itTarget == _sessions.end()) break;

            Message out{ MessageType::ChatPrivate, pc.encode() };
            itTarget->second->sendMessage(out);
            session->sendMessage(out);
            break;
        }
        default:
            break;
        }
    }

    void broadcast(const Message& msg) {
        auto data = msg.encode();
        for (auto& [sid, s] : _sessions) {
            ::send(s->getSocket(), data.data(), (int)data.size(), 0);
        }
    }

private:
    int _port{};
    SOCKET _serverSocket{ INVALID_SOCKET };

    ThreadSafeQueue<ServerEvent> _queue;
    std::atomic<bool> _running{ false };

    std::thread _acceptThread;
    std::thread _eventThread;

    std::atomic<SessionId> _nextSid{ 1 };

    // Only for Event Thread
    std::unordered_map<SessionId, std::shared_ptr<ServerSession>> _sessions;

    // chatterID -> sessionID
    std::unordered_map<std::string, SessionId> _uidToSid; 
};