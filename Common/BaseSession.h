#pragma once
#include "Model.h"

class BaseSession {
public:
    BaseSession(SOCKET socket) : socket(socket) {}

    virtual ~BaseSession() = default;

    SOCKET getSocket() const { return socket; }

    void handleData(const char* data, size_t len) {
        readBuffer.push(data, len);
        Message msg;
        while (readBuffer.tryPopMessage(msg)) {
            onMessage(msg);
        }
    }

    void sendMessage(const Message& message) {
        auto data = message.encode();
        send(socket, data.data(), (int)data.size(), 0);
    }

protected:
    SOCKET socket;
    TcpBuffer readBuffer;
    virtual void onMessage(const Message& message) = 0;
};