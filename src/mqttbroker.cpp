#include <cstring>
#include <iostream>
#include <vector>
#include <cstdint>
#include <stdexcept>
#include <unistd.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include "mqtt_utils.h"
#include "mqttbroker.h"
#include "logger.h"
#include "helper.cpp"

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
        return false;
    }

    buffer.resize(bytes);

    size_t index = 0;

    // Loop through the buffer to handle multiple packets
    while (index < buffer.size())
    {
        size_t packetStartIndex = index; // Save where this specific packet starts
        // classify according to packet type
        uint8_t firstByte = buffer[index++];
        uint8_t type = firstByte >> 4;

        // lower 4 bits with the flags
        uint8_t flags = firstByte & 0x0F;

        // flag check (all EXCEPT PUBLISH)
        if (!isValidFlags(type, flags))
        {
            Logger::log(LEVEL::ERROR, "Protocol Violation: Malformed Packet. Invalid Flags for Type %d", type);
            close(client_fd);
            return false;
        }

        // flags check for PUBLISH packet
        if (type == static_cast<uint8_t>(Signal::PUBLISH))
        {
            // Bit 3: Duplicate Flag
            bool dup = (flags & 0x08) >> 3;

            // Bits 2-1: QoS Level
            // Mask with 0110 (which is 0x06) then shift right by 1
            uint8_t qos = (flags & 0x06) >> 1;

            // Bit 0: Retain Flag
            bool retain = (flags & 0x01);

            // Validation Check: MQTT Spec says QoS 3 (11) is invalid
            if (qos == 3)
            {
                Logger::log(LEVEL::ERROR, "Protocol Violation: QoS 3 is not allowed");
                close(client_fd);
                return false;
            }

            Logger::log(LEVEL::DEBUG, "Publish Detected: QoS %d, Retain %d, Dup %d", qos, retain, dup);
        }

        // remaining length check (2nd byte)
        uint32_t remainingLength;
        try
        {
            remainingLength = decodeVarint(buffer, index);
        }
        catch (...)
        {
            // Malformed length = Protocol Violation
            Logger::log(LEVEL::ERROR, "Protocol Violation: Malformed Length.");
            close(client_fd);
            return false;
        }

        // Check if the actual bytes received match what the header claims
        if (buffer.size() < (index + remainingLength))
        {
            Logger::log(LEVEL::WARNING, "Partial packet received. Waiting for more data...");

            // TODO: In a real broker, you'd save this buffer and wait for more
            break;
        }

        // We save the position where the next packet SHOULD start
        size_t nextPacketIndex = index + remainingLength;

        switch (static_cast<Signal>(type))
        {
        // connect received
        case Signal::CONNECT:
            Logger::log(LEVEL::INFO, "client %d connected", client_fd);
            handleConnect(client_fd, buffer, index, remainingLength);
            break;

        // publish received
        case Signal::PUBLISH:
            // has value other than 0x00 for lower 4 bits of first byte
            handlePublish(client_fd, buffer, index, remainingLength, qos, retain, dup);
            break;

        // subscribe received
        case Signal::SUBSCRIBE:
            handleSubscribe(client_fd, buffer, index, remainingLength);
            break;

        // unsubscribe received
        case Signal::UNSUBSCRIBE:
            handleUnsubscribe(client_fd, buffer, index, remainingLength);
            break;

        case Signal::PINGREQ:
            // Always send PINGRESP immediately
            sendPingResp(client_fd);
            break;

        // puback received (QoS 1 acknowledgement)
        case Signal::PUBACK:
            // buffer elements: fixed header, remaining length, packet id
            if (bytes >= 4)
            {
                uint16_t pid = (buffer[2] << 8) | buffer[3];
                Logger::log(LEVEL::DEBUG, "received PUBACK from %d for packet %u", client_fd, pid);
            }
            break;

        // pubrec received (QoS 2 first step)
        case Signal::PUBREC:
            handlePubrec(client_fd, buffer, index, remainingLength);
            break;

        // pubrel received (QoS 2 second step)
        case Signal::PUBREL:
            handlePubrel(client_fd, buffer, index, remainingLength);
            break;

        // pubcomp received (QoS 2 completion)
        case Signal::PUBCOMP:
            handlePubcomp(client_fd, buffer, index, remainingLength);
            break;

        // disconnect received
        case Signal::DISCONNECT:
            close(client_fd);
            Logger::log(LEVEL::INFO, "client %d disconnected", client_fd);
            return false;

        // auth received (mqtt5.0)

        // unsupported type
        default:
            Logger::log(LEVEL::WARNING, "unsupported packet type: %d", type);
            break;
        }

        // move indx to start of next packet
        index = nextPacketIndex;

        Logger::log(LEVEL::DEBUG, "Processed one packet. Remaining buffer bytes: %zu", buffer.size() - index);
    }

    return true;
}

// handling connect
void mqttbroker::handleConnect(int client_fd, const std::vector<uint8_t> &buffer, size_t &index, uint32_t remainingLength)
{
    // byte 3 to 8 (1 to 6 in the remaining length) for protocol name
    // We need at least 2 bytes for length + 4 bytes for "MQTT" + 1 byte for level = 7 bytes
    if (remainingLength < 7)
    {
        Logger::log(LEVEL::ERROR, "CONNECT packet too short for protocol headers");
        close(client_fd);
        return;
    }

    // protocol check
    uint16_t protoNameLen = (buffer[index] << 8) | buffer[index + 1];
    index += 2;

    if (protoNameLen != 4)
    {
        Logger::log(LEVEL::ERROR, "Protocol Name Length is not 4. Violation.");
        close(client_fd);
        return;
    }

    std::string protoName(reinterpret_cast<const char *>(&buffer[index]), protoNameLen);
    index += protoNameLen;

    if (protoName != "MQTT")
    {
        Logger::log(LEVEL::ERROR, "Protocol Name is not 'MQTT'. Disconnecting.");
        close(client_fd);
        return;
    }

    uint8_t protocolLevel = buffer[index++];

    if (protocolLevel == 4)
    {
        Logger::log(LEVEL::INFO, "Version 3.1.1 detected for client %d", client_fd);
        proceedToV311Checklist(client_fd, buffer, index);
    }
    else if (protocolLevel == 5)
    {
        Logger::log(LEVEL::INFO, "Version 5.0 detected for client %d", client_fd);
        proceedToV50Checklist(client_fd, buffer, index);
    }
    else
    {
        Logger::log(LEVEL::WARNING, "Unsupported Protocol Level: %d. Sending CONNACK 0x01", protocolLevel);

        // Return Code 0x01: Unacceptable protocol version
        // Packet: [CONNACK Type (0x20), Remaining Len (2), Ack Flags (0), Return Code (1)]
        // send connack
        sendConnack(client_fd, 0x00, 0x01);
        close(client_fd);
    }
}

void mqttbroker::proceedToV311Checklist(int client_fd, const std::vector<uint8_t> &buffer, size_t &index) {
    // --- 1. Extract Variable Header ---
    uint8_t connectFlags = buffer[index++];
    
    // Bit breakdown of connectFlags:
    bool reserved     = connectFlags & 0x01; // MUST be 0
    bool cleanSession = connectFlags & 0x02;
    bool willFlag     = connectFlags & 0x04;
    uint8_t willQoS   = (connectFlags & 0x18) >> 3;
    bool willRetain   = connectFlags & 0x20;
    bool passwordFlag = connectFlags & 0x40;
    bool usernameFlag = connectFlags & 0x80;

    if (reserved) { 
        close(client_fd); // Protocol violation
        return; 
    }

    uint16_t keepAlive = (buffer[index] << 8) | buffer[index + 1];
    index += 2;

    // --- 2. Extract Payload (Order matters!) ---
    std::string clientId = get_string(buffer, index);
    
    // Optional fields (only read if flags are set)
    if (willFlag) {
        std::string willTopic = get_string(buffer, index);
        std::string willMsg = get_string(buffer, index);
        // Store these in your session
    }
    if (usernameFlag) std::string user = get_string(buffer, index);
    if (passwordFlag) std::string pass = get_string(buffer, index);

    // --- 3. Handle Session Logic (The QoS/Persistence part) ---
    handleSessionLifecycle(client_fd, clientId, cleanSession);
}

void mqttbroker::handleSessionLifecycle(int client_fd, std::string clientId, bool cleanSession) {
    std::lock_guard<std::mutex> lock(sessionMutex);

    bool sessionPresent = false;
    auto it = sessions.find(clientId);

    if (it != sessions.end()) {
        // EXISTING SESSION FOUND
        if (cleanSession) {
            // Client wants a fresh start: Wipe old data
            sessions.erase(it);
            sessions[clientId] = Session(client_fd, true);
            sessionPresent = false;
        } else {
            // RESUME SESSION: Update socket and prepare to send stored QoS messages
            it->second.socket = client_fd;
            sessionPresent = true;
            resumeDelayedMessages(clientId); 
        }
    } else {
        // NEW SESSION
        sessions[clientId] = Session(client_fd, cleanSession);
        sessionPresent = false;
    }

    // --- 4. Send CONNACK ---
    // Byte 1: 0x20 (Fixed Header)
    // Byte 2: 0x02 (Remaining Length)
    // Byte 3: sessionPresent ? 0x01 : 0x00 (Connect Acknowledge Flags)
    // Byte 4: 0x00 (Connection Accepted Return Code)
    sendConnack(client_fd, sessionPresent, 0x00)
}

void mqttbroker::proceedToV50Checklist(int client_fd, const std::vector<uint8_t> &buffer, size_t &index)
{
}

void mqttbroker::sendConnack(int client_fd, uint8_t ackFlag, uint8_t returnCode) {
    std::vector<uint8_t> packet = {
        0x20,      // Byte 1: CONNACK Header
        0x02,      // Byte 2: Remaining Length
        ackFlag,      // Byte 3: Acknowledge Flags (Session Present = 0)
        returnCode // Byte 4: The variable return code (0x00, 0x01, 0x04, etc.)
    };
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

void sendPingResp(int client_fd)
{
    // TODO: handle functionality
    Logger::log(LEVEL::INFO, "published message to topic '%s': %s", topic.c_str(), message.c_str());
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
