#pragma once
#include "BaseSession.h"
#include "ThreadSafeQueue.h"
#include "ServerModel.h"
#include <atomic>
#include <thread>
#include <optional>

class ServerSession : public BaseSession {
public:
    ServerSession(SessionId sid, SOCKET socket, ThreadSafeQueue<ServerEvent>* queue)
        : BaseSession(socket), _sessionID(sid), _threadQueue(queue) {
    }

    ~ServerSession() override {
        stop();
    }

    SessionId id() const { return _sessionID; }

    // read / write by server main thread
    void setUser(Chatter c) { _user = std::move(c); }

    const std::optional<Chatter>& user() const { return _user; }

    void start() {
        _running.store(true);

        _ioThread = std::thread([this] { 
            Log(std::format("{:<{}} threadid: {}. sid: {} start a new io thread.", "[IO Thread]", GetCurrentThreadId(), _sessionID));
            ioLoop();
            }
        );
    }

    void stop() {
        bool expected = true;
        if (_running.compare_exchange_strong(expected, false)) {
            ::shutdown(socket, SD_BOTH);
            ::closesocket(socket);
        }

        if (_ioThread.joinable()) {
            _ioThread.join();
        }
    }

private:
    void onMessage(const Message& message) override {
        if (_threadQueue) {
            ServerEvent event;
            event.type = ServerEvent::Type::IncomingMsg;
            event.sessionID = _sessionID;
            event.msg = message;
            _threadQueue->push(std::move(event));
        }
    }

    void ioLoop() {
        char buf[4096];
        while (_running.load()) {
            int bytes = ::recv(socket, buf, (int)sizeof(buf), 0);
            if (bytes > 0) {
                handleData(buf, (size_t)bytes);
            }
            else {
                break;
            }
        }

        if (_threadQueue) {
            ServerEvent event;
            event.type = ServerEvent::Type::Disconnected;
            event.sessionID = _sessionID;
            _threadQueue->push(std::move(event));
        }
    }

private:
    SessionId _sessionID{};
    ThreadSafeQueue<ServerEvent>* _threadQueue{ nullptr };

    std::atomic<bool> _running{ false };
    std::thread _ioThread;

    std::optional<Chatter> _user;
};
