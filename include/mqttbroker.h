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

struct Session {
    int socket;
    bool cleanSession;
    std::unordered_set<std::string> subscriptions;
};

struct RemainingLengthResult {
    uint32_t value;
    uint8_t bytesUsed;
};

class MqttBroker {
public:
    MqttBroker(int port);
    void start();
    virtual ~MqttBroker();

private:
    void handleClient(int client_fd);
    bool processPacket(int client_fd); 
   
    void handleConnect(int client_fd);
    void sendConnack(int client_fd);
   
    void handlePublish(int client_fd, const std::vector<uint8_t>& buffer, int bytes);
    void logPublish(int client_fd, const std::string& topic, const std::string& message);
    void forwardToSubscribers(const std::string& topic, const std::string& message, int exclude_fd = -1);
    bool matchTopic(const std::string& sub, const std::string& topic);
   
    void handleSubscribe(int client_fd, const std::vector<uint8_t>& buffer);
    
    int server_fd;
    int port;
    std::unordered_map<std::string, Session> sessions;
    std::unordered_map<std::string, std::unordered_set<std::string>> topicSubscribers;
    std::mutex subMutex;
};