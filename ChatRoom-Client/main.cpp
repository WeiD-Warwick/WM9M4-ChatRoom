#include "Client.h"
//#include "ImGUIDX12.cpp"

#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3d12.lib")


int main() {
    std::string host = "127.0.0.1";
    int port = 65432;

    Log(std::format("[Client] Starting. target={}:{}", host, port));
    std::cout << "Enter your name: ";
    std::string name;
    std::getline(std::cin, name);
    if (name.empty()) {
        std::cout << "Name cannot be empty.\n";
        return 1;
    }

    Client client(host, port);
    if (!client.start(name)) {
        return 1;
    }

    printHelp();
    while (client.isRunning()) {
        std::string line;
        if (!std::getline(std::cin, line)) {
            break;
        }
        if (line.empty()) {
            continue;
        }
        if (line == "/quit") {
            break;
        }
        if (line == "/help") {
            printHelp();
            continue;
        }
        if (line == "/list") {
            client.printUserList();
            continue;
        }
        if (line.rfind("/w ", 0) == 0) {
            auto rest = line.substr(3);
            auto pos = rest.find(' ');
            if (pos == std::string::npos) {
                std::cout << "Usage: /w <id> <message>\n";
                continue;
            }
            std::string id = rest.substr(0, pos);
            std::string content = rest.substr(pos + 1);
            if (content.empty()) {
                std::cout << "Message cannot be empty.\n";
                continue;
            }
            client.sendPrivateMessage(id, content);
            continue;
        }

        client.sendGroupMessage(line);
    }

    client.stop();
    return 0;
}