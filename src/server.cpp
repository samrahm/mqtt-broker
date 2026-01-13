#include "include/server.h"
#include <iostream>

Server::Server(int p) 
    : serverSocket(-1), port(p), running(false) {
}

Server::~Server() {
    if (running) {
        stop();
    }
}

bool Server::start() {
    // TODO: Implement socket binding and listening
    std::cout << "Starting MQTT Broker on port " << port << std::endl;
    running = true;
    return true;
}

void Server::stop() {
    if (running) {
        std::cout << "Stopping MQTT Broker" << std::endl;
        running = false;
    }
}

void Server::run() {
    std::cout << "MQTT Broker running..." << std::endl;
    
    // TODO: Accept client connections in loop
    while (running) {
        // Main server loop
    }
}

void Server::handleClient(int clientSocket) {
    // TODO: Handle client messages
    std::cout << "Handling client connection" << std::endl;
}

void Server::publishMessage(const std::string& topic, const std::string& message) {
    std::cout << "Publishing message to topic '" << topic << "': " << message << std::endl;
    
    // Store message
    topicMessages[topic].push_back(message);
    
    // TODO: Deliver to subscribers
    for (const auto& sub : subscriptions) {
        if (sub.topic == topic) {
            std::cout << "  -> Delivering to client: " << sub.clientId << std::endl;
        }
    }
}

void Server::addSubscription(const std::string& clientId, const std::string& topic) {
    subscriptions.push_back({clientId, topic});
    std::cout << "Client " << clientId << " subscribed to topic: " << topic << std::endl;
}

bool Server::isRunning() const {
    return running;
}
