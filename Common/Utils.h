#pragma once
#include <cstdint>
#include <cstring>
#include <random>
#include <string>
#include <vector>
#include <winsock2.h>

#pragma comment(lib, "ws2_32.lib")

const int uid_length = 11;

inline std::string generateUID(int length = uid_length) {
    static constexpr char table[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";

    static std::mt19937_64 rng{ std::random_device{}() };
    static std::uniform_int_distribution<int> dist(0, 61);

    std::string uid;
    uid.reserve(length);

    for (int i = 0; i < length; ++i) {
        uid.push_back(table[dist(rng)]);
    }
    return uid;
}

inline void serializeString(std::vector<char>& buf, const std::string& str) {
    uint32_t len = htonl((uint32_t)(str.size()));
    const char* lenPtr = reinterpret_cast<const char*>(&len);

    buf.insert(buf.end(), lenPtr, lenPtr + 4);
    buf.insert(buf.end(), str.begin(), str.end());
}

inline std::string deserializeString(const std::vector<char>& buf, size_t& offset) {
    if (offset + 4 > buf.size()) return "";

    uint32_t netLen;
    std::memcpy(&netLen, &buf[offset], 4);
    uint32_t len = ntohl(netLen);
    offset += 4;

    if (offset + len > buf.size()) return "";
    std::string str(&buf[offset], len);
    offset += len;

    return str;
}