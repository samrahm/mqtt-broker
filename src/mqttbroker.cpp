#include <cstring>
#include <iostream>
#include <unistd.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include "mqtt_utils.h"
#include "mqttbroker.h"
#include "logger.h"

#define SUB_ACK 0x90
#define PUBACK_TYPE 0x40  // PUBACK control packet type (4 << 4)
#define PUBREC_TYPE 0x50  // PUBREC control packet type (5 << 4)
#define PUBREL_TYPE 0x62  // PUBREL control packet type (6 << 4) with flags 0b0010
#define PUBCOMP_TYPE 0x70 // PUBCOMP control packet type (7 << 4)

mqttbroker::mqttbroker(int port) : port(port), server_fd(-1) {}

void mqttbroker::start()
{
    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd == 0)
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
        return;
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
    if (server_fd >= 0)
    {
        close(server_fd);
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
            Logger::log(LEVEL::INFO, "processing failed : %d", client_fd);
            break;
        }
    }
    close(client_fd);
}

bool mqttbroker::processPacket(int client_fd)
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
        // close(client_fd);    // redundant call
        return false;
    }

    buffer.resize(bytes);

    // classify according to packet type
    uint8_t firstByte = buffer[0];
    uint8_t packetType = firstByte >> 4; // right shift to keep upper 4 bits containing packet type
    
    // lower 4 bits with the flags 
    uint8_t flags = firstByte & 0x0F;

    // uint8_t qos = (buffer[0] >> 1) & 0x03; // bits 1–2 specifying QoS level ( only for publish ) SWITCH LOGIC PLS 
    switch (static_cast<Signal>(packetType))
    {
    // connect received
    case Signal::CONNECT:
        // error handling for lower 4 bits if they are not 0
        if(flags != 0x00){
            Logger::log(LEVEL::WARNING, "malformed packet. client disconnected");
            close(client_fd);
        }
        Logger::log(LEVEL::INFO, "client %d connected", client_fd);
        handleConnect(client_fd, buffer);
        break;
    
    // publish received
    case Signal::PUBLISH:
        // has value other than 0x00 for lower 4 bits of first byte
        handlePublish(client_fd, buffer, bytes);
        break;

    // subscribe received
    case Signal::SUBSCRIBE:
        if(flags != 0x02){
            Logger::log(LEVEL::WARNING, "malformed packet. client disconnected");
            close(client_fd);
        }    
        handleSubscribe(client_fd, buffer);
        break;
    
    // unsubscribe received
    case Signal::UNSUBSCRIBE:
        if(flags != 0x02){
            Logger::log(LEVEL::WARNING, "malformed packet. client disconnected");
            close(client_fd);
        }    
        handleUnsubscribe(client_fd, buffer);
        break;

    // puback received (QoS 1 acknowledgement)
    case Signal::PUBACK:
        // buffer elements: fixed header, remaining length, packet id
        if(flags != 0x00){
            Logger::log(LEVEL::WARNING, "malformed packet. client disconnected");
            close(client_fd);
        }
        
        if (bytes >= 4)
        {
            uint16_t pid = (buffer[2] << 8) | buffer[3];
            Logger::log(LEVEL::DEBUG, "received PUBACK from %d for packet %u", client_fd, pid);
        }
        break;

    // pubrec received (QoS 2 first step)
    case Signal::PUBREC:
        if(flags != 0x00){
            Logger::log(LEVEL::WARNING, "malformed packet. client disconnected");
            close(client_fd);
        }
        handlePubrec(client_fd, buffer, bytes);
        break;

    // pubrel received (QoS 2 second step)
    case Signal::PUBREL:
        if(flags != 0x02){
            Logger::log(LEVEL::WARNING, "malformed packet. client disconnected");
            close(client_fd);
        }
        handlePubrel(client_fd, buffer, bytes);
        break;

    // pubcomp received (QoS 2 completion)
    case Signal::PUBCOMP:
        if(flags != 0x00){
            Logger::log(LEVEL::WARNING, "malformed packet. client disconnected");
            close(client_fd);
        }    
        handlePubcomp(client_fd, buffer, bytes);
        break;

    // disconnect received
    case Signal::DISCONNECT:
        if(flags != 0x00){
            Logger::log(LEVEL::WARNING, "malformed packet. client disconnected");
            close(client_fd);
        }    
        close(client_fd);
        Logger::log(LEVEL::INFO, "client %d disconnected", client_fd);
        return false;

    // auth received (mqtt5.0)

    // unsupported type
    default:
        Logger::log(LEVEL::WARNING, "unsupported packet type: %d", packetType);
        return false;
    }
    return true;
}

// handling connect
void mqttbroker::handleConnect(int client_fd, const std::vector<uint8_t> &buffer)
{
    

    // byte 3 to 8 (1 to 6 in the remaining length)

    // protocol check 


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

    // prevents other threads from accessing sessions
    std::lock_guard<std::mutex> lock(sessionMutex);

    // create new session or resume existing
    auto it = sessions.find(clientId);
    if (it == sessions.end() || cleanSession)
    {
        sessions[clientId] = Session{client_fd, cleanSession, {}};
    }
    else
    {
        it->second.socket = client_fd; // resume
    }

    // send connack
    sendConnack(client_fd);
}

void mqttbroker::sendConnack(int client_fd)
{
    uint8_t connack[4] = {0x20, 0x02, 0x00, 0x00}; // connack header
    send(client_fd, connack, sizeof(connack), 0);
    Logger::log(LEVEL::INFO, "sent CONNACK to client %d", client_fd);
}

// handling publish
void mqttbroker::handlePublish(int client_fd, const std::vector<uint8_t> &buffer, int bytes)
{
    // parsed through the fixed header
    auto r = parseRemainingLength(buffer);
    size_t fixedHeaderSize = 1 + r.bytesUsed;
    size_t vhOffset = fixedHeaderSize;

    // for QoS > 0 there is a packet identifier in the variable header
    uint8_t flags = buffer[0] & 0x0F;
    uint8_t qos = (flags >> 1) & 0x03;
    uint16_t packetId = 0;
    if (qos > 0)
    {
        packetId = get_uint16(buffer, vhOffset);
        vhOffset += 2;
    }

    uint16_t topicLength = get_uint16(buffer, vhOffset);
    size_t topicOffset = vhOffset + 2;

    // topic extract
    std::string topic(buffer.begin() + topicOffset, buffer.begin() + topicOffset + topicLength);

    // payload extract
    size_t payloadOffset = topicOffset + topicLength;
    std::string payload(buffer.begin() + payloadOffset, buffer.begin() + bytes);

    // log
    logPublish(client_fd, topic, payload);

    // QoS 2 must wait for PUBREL before delivering
    if (qos == 2)
    {
        // store until we see PUBREL
        {
            std::lock_guard<std::mutex> lock(subMutex);
            inflightIncoming[client_fd][packetId] = PendingQoS2{topic, payload};
        }
        uint8_t rec[4];
        rec[0] = PUBREC_TYPE;
        rec[1] = 0x02;
        rec[2] = packetId >> 8;
        rec[3] = packetId & 0xFF;
        send(client_fd, rec, sizeof(rec), 0);
        return;
    }

    // forward to subscribers
    forwardToSubscribers(topic, payload, client_fd);

    // respond with PUBACK for QoS 1
    if (qos == 1)
    {
        uint8_t ack[4];
        ack[0] = PUBACK_TYPE;
        ack[1] = 0x02; // remaining length
        ack[2] = packetId >> 8;
        ack[3] = packetId & 0xFF;
        send(client_fd, ack, sizeof(ack), 0);
        return;
    }
}

void mqttbroker::handlePubrec(int client_fd, const std::vector<uint8_t> &buffer, int bytes)
{
    if (bytes < 4)
        return;

    uint16_t packetId = (buffer[2] << 8) | buffer[3];
    Logger::log(LEVEL::DEBUG, "received PUBREC from %d for packet %u", client_fd, packetId);

    // send PUBREL
    uint8_t rel[4];
    rel[0] = PUBREL_TYPE;
    rel[1] = 0x02;
    rel[2] = packetId >> 8;
    rel[3] = packetId & 0xFF;
    send(client_fd, rel, sizeof(rel), 0);
}

void mqttbroker::handlePubrel(int client_fd, const std::vector<uint8_t> &buffer, int bytes)
{
    if (bytes < 4)
        return;

    uint16_t packetId = (buffer[2] << 8) | buffer[3];
    Logger::log(LEVEL::DEBUG, "received PUBREL from %d for packet %u", client_fd, packetId);

    PendingQoS2 pending;
    {
        std::lock_guard<std::mutex> lock(subMutex);
        auto it = inflightIncoming.find(client_fd);
        if (it != inflightIncoming.end())
        {
            auto it2 = it->second.find(packetId);
            if (it2 != it->second.end())
            {
                pending = it2->second;
                it->second.erase(it2);
            }
        }
    }

    if (!pending.topic.empty())
    {
        forwardToSubscribers(pending.topic, pending.payload, client_fd);
    }

    // respond with PUBCOMP
    uint8_t comp[4];
    comp[0] = PUBCOMP_TYPE;
    comp[1] = 0x02;
    comp[2] = packetId >> 8;
    comp[3] = packetId & 0xFF;
    send(client_fd, comp, sizeof(comp), 0);
}

void mqttbroker::handlePubcomp(int client_fd, const std::vector<uint8_t> &buffer, int bytes)
{
    if (bytes < 4)
        return;

    uint16_t packetId = (buffer[2] << 8) | buffer[3];
    Logger::log(LEVEL::DEBUG, "received PUBCOMP from %d for packet %u", client_fd, packetId);
    std::lock_guard<std::mutex> lock(subMutex);
    auto it = inflightOutgoing.find(client_fd);
    if (it != inflightOutgoing.end())
    {
        it->second.erase(packetId);
    }
}

void mqttbroker::logPublish(int client_fd, const std::string &topic, const std::string &message)
{
    Logger::log(LEVEL::INFO, "published message to topic '%s': %s", topic.c_str(), message.c_str());
}

void mqttbroker::forwardToSubscribers(const std::string &topic, const std::string &message, int exclude_fd)
{
    std::lock_guard<std::mutex> lock(subMutex);

    // find subscribers(each session, mapped on clientid, has subscribers)
    for (const auto &entry : topicSubscribers)
    {
        const std::string &subscription = entry.first;
        if (!matchTopic(subscription, topic))
            continue;

        for (const auto &pair : entry.second)
        {
            int sock = pair.first;
            uint8_t qos = pair.second;
            if (sock == exclude_fd)
                continue;

            std::vector<uint8_t> packet;
            uint8_t headerByte = 0x30; // PUBLISH
            headerByte |= (qos << 1);
            std::string header;
            header.push_back(static_cast<char>(headerByte));
            std::string fullPayload;

            // Build payload: [topic length][topic][message]
            uint16_t len = topic.size();
            fullPayload.push_back((len >> 8) & 0xFF);
            fullPayload.push_back(len & 0xFF);
            fullPayload += topic;

            // add packet id if QoS>0
            uint16_t pid = 0;
            if (qos > 0)
            {
                pid = ++nextPacketId[sock]; // start at 1
                fullPayload.push_back(pid >> 8);
                fullPayload.push_back(pid & 0xFF);

                if (qos == 2)
                {
                    // keep state for QoS 2 handshake (PUBREC/PUBREL/PUBCOMP)
                    inflightOutgoing[sock][pid] = PendingQoS2{topic, message};
                }
            }

            fullPayload += message;

            // remaining length calculation
            uint32_t remLen = fullPayload.size();
            // for simplicity assume <128
            header.push_back(static_cast<char>(remLen));

            packet.insert(packet.end(), header.begin(), header.end());
            packet.insert(packet.end(), fullPayload.begin(), fullPayload.end());

            // send to subscribers
            send(sock, packet.data(), packet.size(), 0);
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
    uint16_t packetId = get_uint16(buffer, 2);

    size_t offset = 4;
    std::vector<uint8_t> returnCodes;
    while (offset + 2 < buffer.size())
    {
        uint16_t topicLen = get_uint16(buffer, offset);
        offset += 2;

        if (offset + topicLen > buffer.size())
            break;

        std::string topic(buffer.begin() + offset, buffer.begin() + offset + topicLen);
        offset += topicLen;

        uint8_t qos = buffer[offset]; // requested QoS
        offset++;

        {
            std::lock_guard<std::mutex> lock(subMutex);
            topicSubscribers[topic][client_fd] = qos;
        }

        Logger::log(LEVEL::INFO, "Client %d subscribed to topic '%s' (QoS %u)", client_fd, topic.c_str(), qos);
        // grant exactly the requested QoS (0,1,2) even if we don't fully support >1
        returnCodes.push_back(qos);
    }

    // Build SUBACK
    std::vector<uint8_t> suback;
    suback.push_back(SUB_ACK);                // SUBACK
    suback.push_back(2 + returnCodes.size()); // Remaining length
    suback.push_back(static_cast<uint8_t>(packetId >> 8));
    suback.push_back(static_cast<uint8_t>(packetId & 0xFF));
    suback.insert(suback.end(), returnCodes.begin(), returnCodes.end());

    send(client_fd, suback.data(), suback.size(), 0);
}
