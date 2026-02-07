#pragma once
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iostream>
#include <unordered_map>
#include <thread>
#include <atomic>
#include <format>

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
        if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
            Log(std::format("{:<{}} WSAStartup failed.", "[Main Thread]", tag_w));
            return false;
        }

        _serverSocket = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (_serverSocket == INVALID_SOCKET) {
            Log(std::format("{:<{}} Socket creation failed.", "[Main Thread]", tag_w));
            WSACleanup();
            return false;
        }

        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = INADDR_ANY;
        addr.sin_port = htons((u_short)_port);

        if (::bind(_serverSocket, (sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR) {
            Log(std::format("{:<{}} Bind failed. port={}", "[Main Thread]", tag_w, _port));
            return false;
        }
        if (::listen(_serverSocket, SOMAXCONN) == SOCKET_ERROR) {
            Log(std::format("{:<{}} Listen failed.", "[Main Thread]", tag_w));
            return false;
        }

        _running.store(true);

        // Accept Thread : accept new connection + create new session + post connection event
        _acceptThread = std::thread([this] { acceptLoop(); });

        // Event Thread : loop events + handle evnets 
        _eventThread = std::thread([this] { eventLoop(); });

        Log(std::format("{:<{}} Server started on port:{}.", "[Main Thread]", tag_w, _port));
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
        Log(std::format("{:<{}} Server stopped.", "[Main Thread]", tag_w));
        WSACleanup();
    }

private:

    // accept client connection and create Session
    void acceptLoop() {
        while (_running.load()) {
            SOCKET clientSock = ::accept(_serverSocket, nullptr, nullptr);
            if (clientSock == INVALID_SOCKET) continue;

            SessionId sid = _nextSid++;

            auto session = std::make_shared<ServerSession>(sid, clientSock, &_queue);

            Log(std::format("{:<{}} A new client connected success. sid: {}.", "[Accept Thread]", tag_w, sid));
            // handle recv
            session->start();

            // push session to sessions to consume for eventThread
            ServerEvent event;
            event.type = ServerEvent::Type::Connected;
            event.sessionID = sid;
            event.session = session;
            _queue.push(std::move(event));


        }
    }

    void eventLoop() {
        ServerEvent event;
        while (_running.load() && _queue.wait_pop(event)) {
            switch (event.type) {
            case ServerEvent::Type::Connected:
                onConnected(event.sessionID, event.session);
                Log(std::format("{:<{}} New Client Connected. sid: {}, totalSession: {}.", "[Event Thread]", tag_w, event.sessionID, _sessions.size()));
                break;
            case ServerEvent::Type::IncomingMsg:
                onIncoming(event.sessionID, event.msg);
                Log(std::format("{:<{}} New Message Incoming. sid: {}, type: {}.", "[Event Thread]", tag_w, event.sessionID, (int)event.msg.type));
                break;
            case ServerEvent::Type::Disconnected:
                onDisconnected(event.sessionID);
                Log(std::format("{:<{}} Client Disconnected. sid: {}, remainingSession: {}.", "[Event Thread]", tag_w, event.sessionID, _sessions.size()));
                break;
            }
        }
    }

private:
    void onConnected(SessionId sid, const std::shared_ptr<ServerSession>& session) {
        _sessions[sid] = session;
        Log(std::format("{:<{}} Session registered. sid: {}.", "[Event Thread]", tag_w, sid));
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
        Log(std::format("{:<{}} Session removed. sid: {}.", "[Event Thread]", tag_w, sid));
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

            Log(std::format("{:<{}} User login. sid: {} id: {} name: {}.", "[Event Thread]", tag_w, sid, me.chatterID, me.chatterName));

            // Send Welcom to client (with current users)
            WelcomeMsg welcome;
            welcome.me = me;
            for (auto& [id, s] : _sessions) {
                if (s->user() && s->user()->chatterID != me.chatterID) {
                    welcome.allUsers.push_back(*s->user());
                }
            }
            Message welcomePacket{ MessageType::Welcome, welcome.encode() };
            session->sendMessage(welcomePacket);

            // broadcast new user
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

            Log(std::format("{:<{}} Group message. sid: {} id: {}.", "[Event Thread]", tag_w, sid, gc.sender.chatterID));

            Message out { MessageType::ChatGroup, gc.encode() };
            broadcast(out);
            break;
        }
        case MessageType::ChatPrivate: {
            if (!session->user()) break;
            PrivateChat pc = PrivateChat::decode(msg.body);

            // sender base on session
            pc.sender = *session->user();

            Log(std::format("{:<{}} Private message. from: {} to: {}.", "[Event Thread]", tag_w, pc.sender.chatterID, pc.receiver.chatterID));

            auto itSid = _uidToSid.find(pc.receiver.chatterID);
            if (itSid == _uidToSid.end()) {
                // offline
                SystemMessage event{ pc.receiver, "Receiver not online" };
                Message systemMessage { MessageType::SystemMessage, event.encode() };

                // send system message to sender
                session->sendMessage(systemMessage);

                Log(std::format("{:<{}} Receiver offline. from: {} to: {}.", "[Event Thread]", tag_w, pc.sender.chatterID, pc.receiver.chatterID));
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
        Log(std::format("{:<{}} Broadcast type: {} to {} sessions.", "[Event Thread]", tag_w, (int)msg.type, _sessions.size()));
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