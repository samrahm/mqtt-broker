#pragma once

#include <string>
#include <thread>
#include <netinet/in.h>

enum class Signal
{
    CONNECT=1,
    PUBLISH=3,
    SUBSCRIBE=8,
    DISCONNECT=14
};  

class MqttBroker {
public:
    MqttBroker(int port);
    void start();
    virtual ~MqttBroker();

private:
    void handleClient(int clientSock);
    bool processPacket(int clientSock); 
    void sendConnack(int clientSock);
    void logPublish(int clientSock, const std::string& topic, const std::string& message);
    bool matchTopic(const std::string& sub, const std::string& topic);

    void handlePublish(int clientSock, const std::vector<uint8_t>& buffer, int bytes);
    void handleSubscribe(int clientSock, const std::vector<uint8_t>& buffer);
    void forwardToSubscribers(const std::string& topic, const std::string& message, int excludeSock = -1);
    
    int serverSock;
    int port;
    std::unordered_map<std::string, std::unordered_set<int>> topicSubscribers;
    std::mutex subMutex;
};