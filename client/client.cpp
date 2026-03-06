#include "client.h"
#include <iostream>

Client::Client(const std::string& id) 
    : clientId(id), socket(-1), connected(false) {
}

Client::~Client() {
    if (connected) {
        disconnect();
    }
}

bool Client::connect(const std::string& host, int port) {
    // TODO: Implement socket connection
    std::cout << "Client " << clientId << " connecting to " << host << ":" << port << std::endl;
    connected = true;
    return true;
}

void Client::disconnect() {
    if (connected) {
        std::cout << "Client " << clientId << " disconnecting" << std::endl;
        connected = false;
    }
}

bool Client::publish(const std::string& topic, const std::string& message) {
    if (!connected) {
        std::cerr << "Client not connected" << std::endl;
        return false;
    }
    
    std::cout << "Publishing to topic '" << topic << "': " << message << std::endl;
    return true;
}

bool Client::subscribe(const std::string& topic, std::function<void(const std::string&)> callback) {
    if (!connected) {
        std::cerr << "Client not connected" << std::endl;
        return false;
    }
    
    std::cout << "Subscribing to topic: " << topic << std::endl;
    return true;
}

bool Client::isConnected() const {
    return connected;
}

const std::string& Client::getClientId() const {
    return clientId;
}
