#include "Server.h"
#include <iostream>

int main() {
    Server server(65432);
    if (!server.start()) {
        std::cout << "Server start failed.\n";
        return 1;
    }

    std::string line;
    std::getline(std::cin, line);

    server.stop();
    return 0;
}
