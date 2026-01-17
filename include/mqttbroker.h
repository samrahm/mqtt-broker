#pragma once

#include <string>
#include <thread>
#include <netinet/in.h>


class MqttBroker {
public:
    MqttBroker(int port);
    void start();
    virtual ~MqttBroker();

private:
    void handleClient(int clientSock);
    bool processPacket(int clientSock);
}