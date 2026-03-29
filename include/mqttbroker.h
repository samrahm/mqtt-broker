#pragma once

#include <string>
#include <thread>
#include <netinet/in.h>
#include <vector>
#include <unordered_map>
#include <map>
#include <set>
#include <queue>
#include <unordered_set>
#include <mutex>
#include <sstream>
#include <chrono>
#include <ctime>

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

enum class MessageStatus
{
    SENDING,
    AWAITING_PUBACK,
    AWAITING_PUBREC,
    AWAITING_PUBCOMP
};

// MQTT Message with metadata
struct MQTTMessage
{
    std::string topic;
    std::string payload;
    uint8_t qos;
    bool retain;
    MessageStatus status;
    std::time_t timestamp; // For retry logic (when was this message sent?)
    uint16_t packetId;     // Assigned packet ID

    MQTTMessage() : qos(0), retain(false), status(MessageStatus::SENDING),
                    timestamp(std::time(nullptr)), packetId(0) {}

    MQTTMessage(const std::string &t, const std::string &p, uint8_t q = 0, bool r = false)
        : topic(t), payload(p), qos(q), retain(r), status(MessageStatus::SENDING),
          timestamp(std::time(nullptr)), packetId(0) {}
};

struct Session
{
    int socket;
    bool cleanSession;
    uint16_t nextPacketId; // For this session (ranges from 1 to 65535)
    std::string willTopic;
    std::string willPayload;
    uint8_t willQoS;
    bool willRetain;
    bool willFlag; // Track if will was set

    std::unordered_set<std::string> subscriptions;
    // For Non-Clean Sessions:
    std::queue<MQTTMessage> offlineQueue;     // Messages to send upon reconnect
    std::map<uint16_t, MQTTMessage> inflight; // PacketId -> Message (awaiting ACK)
    std::set<uint16_t> qos2Receiving;         // PacketIds currently in the 4-way handshake

    // Constructor
    Session(int sock = -1, bool clean = true)
        : socket(sock), cleanSession(clean), nextPacketId(0),
          willQoS(0), willRetain(false), willFlag(false) {}
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
    bool processPacket(int client_fd, bool &gracefulDisconnect);

    void handleConnect(int client_fd, const std::vector<uint8_t> &buffer, size_t &index, uint32_t remainingLength);
    void sendConnack(int client_fd, uint8_t ackFlag, uint8_t returnCode);
    void handleSessionLifecycle(int client_fd, const std::string &clientId, bool cleanSession,
                                std::string willTopic = "", std::string willPayload = "",
                                uint8_t willQoS = 0, bool willRetain = false, bool willFlag = false);
    void resumeDelayedMessages(const std::string &clientId, int client_fd);
    void proceedToV311Checklist(int client_fd, const std::vector<uint8_t> &buffer, size_t &index);
    void proceedToV50Checklist(int client_fd, const std::vector<uint8_t> &buffer, size_t &index);

    void handlePublish(int client_fd, const std::vector<uint8_t> &buffer, size_t index, uint32_t remainingLength, uint8_t qos, bool retain, bool dup);
    void handlePuback(int client_fd, const std::vector<uint8_t> &buffer, size_t index, uint32_t remainingLength);
    void handlePubrec(int client_fd, const std::vector<uint8_t> &buffer, size_t index, uint32_t remainingLength);
    void handlePubrel(int client_fd, const std::vector<uint8_t> &buffer, size_t index, uint32_t remainingLength);
    void handlePubcomp(int client_fd, const std::vector<uint8_t> &buffer, size_t index, uint32_t remainingLength);
    void sendPingResp(int client_fd);
    void sendPublishToClient(int client_fd, const MQTTMessage &msg, uint16_t packetId, bool dup);
    uint16_t generatePacketId(const std::string &clientId);

    void logPublish(int client_fd, const std::string &topic, const std::string &message);
    void forwardToSubscribers(const std::string &topic, const std::string &message, int exclude_fd = -1);
    bool matchTopic(const std::string &sub, const std::string &topic);
    void internalPublish(const std::string &topic, const std::string &payload, uint8_t qos, bool retain);

    void handleSubscribe(int client_fd, const std::vector<uint8_t> &buffer, size_t &index, uint32_t remainingLength);
    void handleUnsubscribe(int client_fd, const std::vector<uint8_t> &buffer, size_t index, uint32_t remainingLength);
    void triggerLastWill(const std::string &clientId);
    void retryInflightMessages();

    int server_fd;
    int port;
    std::unordered_map<std::string, Session> sessions; // clientId -> session
    std::unordered_map<int, std::string> clientIdMap;  // socket fd -> clientId (for tracking disconnects)
    // topic -> (socket -> qos level requested)
    std::unordered_map<std::string, std::unordered_map<int, uint8_t>> topicSubscribers;

    // QoS 2 state tracking
    // inbound: message received with PUBLISH (QoS 2) from clients, awaiting PUBREL
    std::unordered_map<int, std::unordered_map<uint16_t, PendingQoS2>> inflightIncoming;
    // outbound: PUBLISH (QoS 2) sent to subscribers awaiting PUBREC/PUBCOMP
    std::unordered_map<int, std::unordered_map<uint16_t, PendingQoS2>> inflightOutgoing;

    std::mutex sessionMutex;
    std::mutex subMutex;
    std::thread retryThread;
};