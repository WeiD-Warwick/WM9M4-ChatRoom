#pragma once
#include <cstdint>
#include <cstring>
#include <string>
#include <variant>
#include <vector>
#include <winsock2.h>
#include <memory>

#include "Utils.h"
#include <iostream>

namespace Protocol {
    const size_t LENGTH_FIELD_SIZE = 4; // uint32_t
    const size_t TYPE_FIELD_SIZE = 2;   // uint16_t
    const size_t HEADER_SIZE = LENGTH_FIELD_SIZE + TYPE_FIELD_SIZE;
}

enum class MessageType : uint16_t {
    Hello = 1,          // client->server   :  [name]
    Welcome = 2,        // server->client   :  [chatter]
    ChatGroup = 3,      // client<->server  :  [chatter][content]
    ChatPrivate = 4,    // client->target   :  [chatter][chatter][content]
    UserJoin = 5,       // server->all      :  [chatter][text]
    UserLeave = 6,      // server->all      :  [chatter][text]
    SystemMessage = 7,  // server->client   :  [chatter][text]
};

struct Chatter {
    std::string chatterID;
    std::string chatterName;

    Chatter(std::string id, std::string name) : chatterID(id), chatterName(name) {}

    Chatter(std::string name) : Chatter(generateUID(), name) {}

    Chatter() {}

    std::string encode() const {
        std::vector<char> buf;
        serializeString(buf, chatterID);
        serializeString(buf, chatterName);
        return std::string(buf.begin(), buf.end());
    }

    static Chatter decode(const std::string& data) {
        std::vector<char> buf(data.begin(), data.end());
        size_t offset = 0;
        std::string id = deserializeString(buf, offset);
        std::string name = deserializeString(buf, offset);
        return Chatter(id, name);
    }

    bool isEmpty() { return chatterID.empty() && chatterName.empty(); }
};

struct HelloMsg {
    std::string name;

    std::string encode() const {
        std::vector<char> buf;
        serializeString(buf, name);
        return std::string(buf.begin(), buf.end());
    }

    static HelloMsg decode(const std::string& data) {
        std::vector<char> buf(data.begin(), data.end());
        size_t offset = 0;
        return { deserializeString(buf, offset) };
    }
};

struct WelcomeMsg {
    Chatter me;
    std::vector<Chatter> allUsers;

    std::string encode() const {
        std::vector<char> buf;
        serializeString(buf, me.encode());

        uint32_t count = static_cast<uint32_t>(allUsers.size());
        uint32_t netCount = htonl(count);
        char countBuf[4];
        std::memcpy(countBuf, &netCount, 4);
        buf.insert(buf.end(), countBuf, countBuf + 4);

        for (const auto& user : allUsers) {
            serializeString(buf, user.encode());
        }
        return std::string(buf.begin(), buf.end());
    }

    static WelcomeMsg decode(const std::string& data) {
        std::vector<char> buf(data.begin(), data.end());
        size_t offset = 0;

        WelcomeMsg msg;
        msg.me = Chatter::decode(deserializeString(buf, offset));

        uint32_t netCount = 0;
        std::memcpy(&netCount, buf.data() + offset, 4);
        offset += 4;
        uint32_t count = ntohl(netCount);

        for (uint32_t i = 0; i < count; ++i) {
            msg.allUsers.push_back(Chatter::decode(deserializeString(buf, offset)));
        }
        return msg;
    }
};

struct GroupChat {
    Chatter sender;
    std::string content;

    std::string encode() const {
        std::vector<char> buf;
        serializeString(buf, sender.encode());
        serializeString(buf, content);
        return std::string(buf.begin(), buf.end());
    }

    static GroupChat decode(const std::string& data) {
        std::vector<char> buf(data.begin(), data.end());
        size_t offset = 0;
        Chatter s = Chatter::decode(deserializeString(buf, offset));
        std::string c = deserializeString(buf, offset);
        return { s, c };
    }
};

struct PrivateChat {
    Chatter sender;
    Chatter receiver;
    std::string content;

    std::string encode() const {
        std::vector<char> buf;
        std::string sData = sender.encode();
        serializeString(buf, sData);

        std::string rData = receiver.encode();
        serializeString(buf, rData);

        serializeString(buf, content);
        return std::string(buf.begin(), buf.end());
    }

    static PrivateChat decode(const std::string& data) {
        std::vector<char> buf(data.begin(), data.end());
        size_t offset = 0;

        std::string sData = deserializeString(buf, offset);
        Chatter s = Chatter::decode(sData);

        std::string rData = deserializeString(buf, offset);
        Chatter r = Chatter::decode(rData);

        std::string c = deserializeString(buf, offset);

        return { s, r, c };
    }
};

// Join
struct UserJoin {
    Chatter user;
    std::string text;

    std::string encode() const {
        std::vector<char> buf;
        serializeString(buf, user.encode());
        serializeString(buf, text);
        return std::string(buf.begin(), buf.end());
    }

    static UserJoin decode(const std::string& data) {
        std::vector<char> buf(data.begin(), data.end());
        size_t offset = 0;
        Chatter u = Chatter::decode(deserializeString(buf, offset));
        std::string t = deserializeString(buf, offset);
        return { u, t };
    }
};

struct UserLeave {
    Chatter user;
    std::string text;

    std::string encode() const {
        std::vector<char> buf;
        serializeString(buf, user.encode());
        serializeString(buf, text);
        return std::string(buf.begin(), buf.end());
    }

    static UserLeave decode(const std::string& data) {
        std::vector<char> buf(data.begin(), data.end());
        size_t offset = 0;
        Chatter u = Chatter::decode(deserializeString(buf, offset));
        std::string t = deserializeString(buf, offset);
        return { u, t };
    }
};

struct SystemMessage {
    Chatter user;
    std::string text;

    std::string encode() const {
        std::vector<char> buf;
        serializeString(buf, user.encode());
        serializeString(buf, text);
        return std::string(buf.begin(), buf.end());
    }

    static SystemMessage decode(const std::string& data) {
        std::vector<char> buf(data.begin(), data.end());
        size_t offset = 0;
        Chatter u = Chatter::decode(deserializeString(buf, offset));
        std::string t = deserializeString(buf, offset);
        return { u, t };
    }
};

using BusinessMsg = std::variant<
    HelloMsg,
    WelcomeMsg,
    Chatter,
    GroupChat,
    PrivateChat,
    UserJoin,
    UserLeave,
    SystemMessage
>;

struct Message {
    MessageType type{};
    std::string body;

    std::vector<char> encode() const {
        uint32_t bodySize = static_cast<uint32_t>(body.size());
        uint32_t totalPayloadSize = Protocol::TYPE_FIELD_SIZE + bodySize;

        uint32_t netLength = htonl(totalPayloadSize);
        uint16_t netType = htons(static_cast<uint16_t>(type));

        std::vector<char> packet;
        packet.reserve(Protocol::LENGTH_FIELD_SIZE + totalPayloadSize);

        // encode type
        char lenBuf[4], typeBuf[2];
        std::memcpy(lenBuf, &netLength, 4);
        std::memcpy(typeBuf, &netType, 2);

        packet.insert(packet.end(), lenBuf, lenBuf + 4);
        packet.insert(packet.end(), typeBuf, typeBuf + 2);

        // encode body
        packet.insert(packet.end(), body.begin(), body.end());

        return packet;
    }

    BusinessMsg decode() const {
        switch (type) {
        case MessageType::Hello:       return HelloMsg::decode(body);
        case MessageType::Welcome:     return WelcomeMsg::decode(body);
        case MessageType::ChatGroup:   return GroupChat::decode(body);
        case MessageType::ChatPrivate: return PrivateChat::decode(body);
        case MessageType::UserJoin:    return UserJoin::decode(body);
        case MessageType::UserLeave:   return UserLeave::decode(body);
        default:                       return HelloMsg{ "" };
        }
    }
};

class TcpBuffer {
public:

    void push(const char* data, size_t size) {
        buffer.insert(buffer.end(), data, data + size);
    }

    bool tryPopMessage(Message& outMsg) {
        // cant get length info
        if (buffer.size() < Protocol::HEADER_SIZE) return false;

        // Parse length (4 bits)
        uint32_t netLength = 0;
        std::memcpy(&netLength, buffer.data(), 4);
        uint32_t totalPayloadSize = ntohl(netLength);

        // cant get full (type + body) info
        if (buffer.size() < Protocol::LENGTH_FIELD_SIZE + totalPayloadSize) {
            return false;
        }

        // Parse type (2 bits)
        uint16_t netType = 0;
        std::memcpy(&netType, buffer.data() + Protocol::LENGTH_FIELD_SIZE, 2);
        outMsg.type = static_cast<MessageType>(ntohs(netType));

        // Parse true body content
        size_t bodySize = totalPayloadSize - Protocol::TYPE_FIELD_SIZE;
        if (bodySize > 0) {
            outMsg.body.assign(
                buffer.begin() + Protocol::HEADER_SIZE,
                buffer.begin() + Protocol::HEADER_SIZE + bodySize
            );
        }
        else {
            outMsg.body.clear();
        }

        // remove processed data
        buffer.erase(buffer.begin(), buffer.begin() + Protocol::LENGTH_FIELD_SIZE + totalPayloadSize);
        return true;
    }

    size_t size() const { return buffer.size(); }
    void clear() { buffer.clear(); }

private:
    std::vector<char> buffer;
};