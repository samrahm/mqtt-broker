#include <cstring>
#include <iostream>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#include "include/mqtt_utils.h"
#include "include/logger.h"

mqttbroker::mqttbroker(int port) : port(port), serversock(-1) {}

void mqttbroker::start()
{
    serversock = socket(AF_INET, SOCK_STREAM, 0);
    if (serversock == 0)
    {
        Logger::log(LEVEL::WARNING, "socket creation failed.");
        return;
    }

    sockaddr_in address;
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(port);

    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) == -1)
    {
        Logger::log(LEVEL::WARNING, "bind failed on port %d", port);
        retun
    }

    if (listen(server_fd, 3) == -1)
    {
        Logger::log(LEVEL::WARNING, "Listen failed on port %d", port);
        return;
    }

    Logger::log(LEVEL::INFO, "MQTT Broker started on port %d", port);

    while (true)
    {
        int addrlen = sizeof(address);
        int client_fd = accept(server_fd, (struct sockaddr *)&address, (socklen_t *)&addrlen);
        if (client_fd < 0)
        {
            Logger::log(LEVEL::WARNING, "failed to accept %d ", client_fd);
            continue;
        }

        std::thread(&mqttbroker::handleClient, this, client_fd).detach();
    }
}

mqttbroker::~mqttbroker()
{
    if (serversock >= 0)
    {
        close(serversock);
        Logger::log(LEVEL::INFO, "MQTT Broker stopped");
    }
}

// client handling
void mqttbroker::handleClient(int client_fd)
{
    while (true)
    {
        if (!processPacket(client_fd))
        {
            Logger::log(LEVEL::INFO, "client disconnected : %d", client_fd);
            break;
        }
    }
    close(client_fd);
}

void mqttbroker::processPacket(int client_fd)
{
    // read packet
    std::vector<uint8_t> buffer(1024);
    int bytes = recv(client_fd, buffer.data(), buffer.size(), 0);
    if (bytes <= 0)
    {
        if (bytes < 0)
        {
            Logger::log(LEVEL::WARNING, "client disconnected or error ");
            Logger::log(LEVEL::WARNING, "receive failed on socket %d", client_fd);
        }
        buffer.clear();
        close(client_fd);
        return false;
    }

    buffer.resize();

    // classify according to packet type

    uint8_t packetType = buffer[0] >> 4; // right shift to keep upper 4 bits containing packet type
    switch (static_cast<Signal>(packetType))
    {
    // connect received
    case Signal::CONNECT:
        Logger::log(LEVEL::INFO, "client %d connected", client_fd);
        handleConnect(client_fd);
        break;

    // publish received
    case Signal::PUBLISH:
        handlePublish(client_fd, buffer, bytes);
        break;

    // subscribe received
    case Signal::SUBSCRIBE:
        handleSubscribe(client_fd, buffer);
        break;

    // disconnect received
    case Signal::DISCONNECT:
        close(client_fd);
        Logger::log(LEVEL::INFO, "client %d disconnected", client_fd);
        return false;

    // unsupported type
    default:
        Logger::log(LEVEL::WARNING, "unsupported packet type: %d", packetType);
        return false;
    }
    return true;
}

// handling connect
void mqttbroker::handleConnect(int client_fd)
{
    // parse connect
    auto rl = parseRemainingLength(buffer);
    size_t fixedHeaderSize = 1 + rl.bytesUsed;

    size_t offset = fixedHeaderSize;

    // check protocol/ version
    std::string protocolName = get_string(buffer, offset);

    uint8_t protocolLevel = get_uint8(buffer, offset);
    offset += 1;

    uint8_t connectFlags = get_uint8(buffer, offset);
    offset += 1;

    uint16_t keepAlive = get_uint16(buffer, offset);
    offset += 2;

    // extract clientID, clean session
    std::string clientId = get_string(buffer, offset);
    bool cleanSession = connectFlags & 0x02;

    //prevents other threads from accessing sessions
    std::lock_guard<std::mutex> lock(sessionMutex);

    // create new session or resume existing
    if (!sessions.contains(clientId) || cleanSession)
    {
        sessions[clientId] = Session{client_fd, cleanSession, {}};
    }
    else
    {
        sessions[clientId].socket = client_fd; // resume
    }

    // send connack
    sendConnack(client_fd);
}

void mqttbroker::sendConnack(int client_fd)
{
    uint8_t connack[4] = {0x20, 0x02, 0x00, 0x00}; // connack header
    send(client_fd, connack, sizeof(connack), 0);
    Logger::log(LEVEL::INFO, "sent CONNACK to client %d", clientSock);
}

// handling publish
void mqttbroker::handlePublish(int client_fd, const std::vector<uint8_t> &buffer, int bytes)
{
    // parsed through the fixed header
    auto r = parseRemainingLength(buffer);
    size_t fixedHeaderSize = 1 + r.bytesUsed;
    size_t vhOffset = fixedHeaderSize;

    uint16_t topicLength = get_uint16(buffer, vhOffset);
    size_t topicOffset = vhOffset + 2;

    // topic extract
    std::string topic(buffer.begin() + topicOffset, buffer.begin() + topicOffset + topicLength);

    // payload extract
    size_t payloadOffset = topicOffset + topicLength;
    std::string payload(buffer.begin() + payloadOffset, buffer.begin() + bytes);

    // log
    logPublish(cient_fd, topic, payload);
    // forward to subscribers
    forwardToSubscribers(topic, payload, client_fd);
}

void mqttbroker::logPublish(int client_fd, const std::string &topic, const std::string &message)
{
    Logger::log(LEVEL::INFO, "published message to topic '%s': %s", topic.c_str(), message.c_str());
}

void forwardToSubscribers(const std::string &topic, const std::string &message, int exclude_fd)
{
    std::lock_guard<std::mutex> lock(subMutex);
    
    // find subscribers(each session, mapped on clientid, has subscribers)
    for (const auto &entry : topicSubscribers)
    {
        const std::string &subscription = entry.first;
        const std::unordered_set<int> &sockets = entry.second;

        if (matchTopic(subscription, topic))
        {
            for (int sock : sockets)
            {
                if (sock == excludeSock)
                    continue;

                std::vector<uint8_t> packet;
                std::string header = "\x30"; // PUBLISH, QoS 0
                std::string fullPayload;

                // Build payload: [topic length][topic][message]
                uint16_t len = topic.size();
                // encodes topic length 
                fullPayload.push_back((len >> 8) & 0xFF);
                fullPayload.push_back(len & 0xFF);
                fullPayload += topic;
                fullPayload += message;

                header += static_cast<char>(fullPayload.size());
                packet.insert(packet.end(), header.begin(), header.end());
                packet.insert(packet.end(), fullPayload.begin(), fullPayload.end());

                // send to subscribers
                send(sock, packet.data(), packet.size(), 0);
            }
        }
    }


}


bool mqttbroker::matchTopic(const std::string &subscription, const std::string &topic)
{
    Logger::log(LEVEL::DEBUG, "Matching subscription '%s' with topic '%s'", subscription.c_str(), topic.c_str());

    std::istringstream subStream(subscription);
    std::istringstream topicStream(topic);

    std::string subToken, topicToken;

    while (true)
    {
        bool hasSub = static_cast<bool>(std::getline(subStream, subToken, '/'));
        bool hasTopic = static_cast<bool>(std::getline(topicStream, topicToken, '/'));

        if (!hasSub && !hasTopic)
        {
            // Reached end of both, full match
            return true;
        }

        if (hasSub && subToken == "#")
        {
            return true; // Match everything after
        }

        if (!hasSub || !hasTopic)
        {
            // One stream ended before the other, not a match
            return false;
        }

        if (subToken != "+" && subToken != topicToken)
        {
            return false;
        }
    }

    return true;
}

// handling subscribe
void mqttbroker::handleSubscribe(int client_fd, const std::vector<uint8_t> &buffer)
{
}
