#pragma once
#include "BaseSession.h"
#include <atomic>
#include <thread>
#include <format>
#include "ThreadSafeQueue.h"
#include "ClientModel.h"

class ClientSession : public BaseSession {

public:
    ClientSession(SOCKET socket, ThreadSafeQueue<ClientEvent>* queue)
        : BaseSession(socket), _threadQueue(queue) {
    }

    ~ClientSession() override {
        stop();
    }

    void start() {
        _running.store(true);
        _ioThread = std::thread([this] {
            Log("[ClientSession] IO thread started.");
            ioLoop();
            });
    }

    void stop() {
        bool expected = true;
        if (_running.compare_exchange_strong(expected, false)) {
            Log("[ClientSession] Stopping session.");
            ::shutdown(socket, SD_BOTH);
            ::closesocket(socket);
        }

        if (_ioThread.joinable()) {
            _ioThread.join();
        }
    }

private:
    void onMessage(const Message& message) override {
        (void)message;
        if (_threadQueue) {
            ClientEvent event;
            event.type = ClientEvent::Type::IncomingMsg;
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
                if (_threadQueue) {
                    ClientEvent event;
                    if (bytes == SOCKET_ERROR) {
                        Log(std::format("[ClientSession] Socket recv error: {}", WSAGetLastError()));
                        event.type = ClientEvent::Type::Error;
                        event.err = WSAGetLastError();
                    }
                    else {
                        Log("[ClientSession] Server closed connection.");
                        event.type = ClientEvent::Type::Disconnected;
                    }
                    _threadQueue->push(std::move(event));
                }
                break;
            }
        }
    }

private:
    ThreadSafeQueue<ClientEvent>* _threadQueue { nullptr };
    std::atomic<bool> _running{ false };
    std::thread _ioThread;

};