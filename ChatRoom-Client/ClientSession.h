#pragma once
#include "BaseSession.h"

class ClientSession : public BaseSession {

    MessageHandler handler;

    void onMessage(const Message& message) override {
        std::visit(handler, message.decode());
    }
};

struct MessageHandler {
    void operator()(const HelloMsg& msg) const {
        std::cout << ">> [Handle Hello] ChatterName: " << msg.name << std::endl;
    }
    void operator()(const Chatter& msg) const {
        std::cout << ">> [Handle Welcome] Welcome, " << msg.chatterName
            << " (ID: " << msg.chatterID << ")" << std::endl;
    }
    void operator()(const GroupChat& msg) const {
        std::cout << ">> [Group Message] " << msg.sender.chatterName << ": " << msg.content << std::endl;
    }
    void operator()(const PrivateChat& msg) const {
        std::cout << ">> [Private Message] " << msg.sender.chatterName << " -> "
            << msg.receiver.chatterName << ": " << msg.content << std::endl;
    }
    void operator()(const SystemMessage& msg) const {
        std::cout << ">> [Event] " << msg.user.chatterName << " " << msg.text << std::endl;
    }
};