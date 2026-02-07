#pragma once
#include "BaseSession.h"
#include <atomic>
#include <thread>

class ClientSession : public BaseSession {

public:
    explicit ClientSession(SOCKET socket)
        : BaseSession(socket) {
    }

    ~ClientSession() override {
        stop();
    }

    void start() {
        _running.store(true);
        _ioThread = std::thread([this] {
            ioLoop();
            });
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
        (void)message;
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
    }

private:
    std::atomic<bool> _running{ false };
    std::thread _ioThread;

};