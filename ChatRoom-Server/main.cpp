#include "Server.h"
#include <iostream>

int main() {
    Server server(65432);
    if (!server.start()) {
        Log(std::format("{:<{}} Server start failed.", "[Main Thread]", tag_w));
        return 1;
    }

    std::string line;
    std::getline(std::cin, line);

    server.stop();
    return 0;
}
