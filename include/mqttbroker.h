#pragma once

#include <string>
#include <thread>
#include <netinet/in.h>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <mutex>
#include <sstream>

enum class Signal
{
    CONNECT = 1,
    CONNACK = 2, // sent by the broker
    PUBLISH = 3,
    PUBACK = 4,
    PUBREC = 5,
    PUBREL = 6,
    PUBCOMP = 7,
    SUBSCRIBE = 8,
    SUBACK = 9,       // sent by the broker
    UNSUBSCRIBE = 10, // sent by the broker
    UNSUBACK = 11,
    PINGREQ = 12,
    PINGRESP = 13,
    DISCONNECT = 14,
    AUTH = 15
};

struct Session
{
    int socket;
    bool cleanSession;
    std::unordered_set<std::string> subscriptions;
    // unacknowledged qos msgs. queued msgs,
};

struct PendingQoS2
{
    std::string topic;
    std::string payload;
};

class mqttbroker
{
public:
    mqttbroker(int port);
    void start();
    virtual ~mqttbroker();

private:
    void handleClient(int client_fd);
    bool processPacket(int client_fd);

    void handleConnect(int client_fd, const std::vector<uint8_t> &buffer, size_t index, uint32_t remainingLength);
    void sendConnack(int client_fd);

    void handlePublish(int client_fd, const std::vector<uint8_t> &buffer, size_t index, uint32_t remainingLength, uint8_t flags);
    void handlePubrec(int client_fd, const std::vector<uint8_t> &buffer, size_t index, uint32_t remainingLength);
    void handlePubrel(int client_fd, const std::vector<uint8_t> &buffer, size_t index, uint32_t remainingLength);
    void handlePubcomp(int client_fd, const std::vector<uint8_t> &buffer, size_t index, uint32_t remainingLength);
    void sendPingResp(int client_fd);
        

    void logPublish(int client_fd, const std::string &topic, const std::string &message);
    void forwardToSubscribers(const std::string &topic, const std::string &message, int exclude_fd = -1);
    bool matchTopic(const std::string &sub, const std::string &topic);

    void handleSubscribe(int client_fd, const std::vector<uint8_t> &buffer, size_t index, uint32_t remainingLength);
    void handleUnsubscribe(int client_fd, const std::vector<uint8_t> &buffer, size_t index, uint32_t remainingLength);

    int server_fd;
    int port;
    std::unordered_map<std::string, Session> sessions; // client -> session
    // topic -> (socket -> qos level requested)
    std::unordered_map<std::string, std::unordered_map<int, uint8_t>> topicSubscribers;
    // track next packet identifier to send for each client (used when forwarding QoS>0)
    std::unordered_map<int, uint16_t> nextPacketId;

    // QoS 2 state tracking
    // inbound: message received with PUBLISH (QoS 2) from clients, awaiting PUBREL
    std::unordered_map<int, std::unordered_map<uint16_t, PendingQoS2>> inflightIncoming;
    // outbound: PUBLISH (QoS 2) sent to subscribers awaiting PUBREC/PUBCOMP
    std::unordered_map<int, std::unordered_map<uint16_t, PendingQoS2>> inflightOutgoing;

    std::mutex sessionMutex;
    std::mutex subMutex;
};