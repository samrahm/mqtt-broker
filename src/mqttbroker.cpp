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

mqttbroker::mqttbroker(int port) : server_fd(-1), port(port) {}

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

void mqttbroker::handleClient(int client_fd)
{
    bool gracefulDisconnect = false;
    std::string clientId = "";

    Logger::log(LEVEL::INFO, "New TCP connection established: fd %d", client_fd);

    // 1. Run the packet processing loop
    while (processPacket(client_fd, gracefulDisconnect))
    {
        // If we haven't identified the client yet, try to find them in the map
        if (clientId.empty())
        {
            std::lock_guard<std::mutex> lock(sessionMutex);
            if (clientIdMap.count(client_fd))
            {
                clientId = clientIdMap[client_fd];
            }
        }
    }

    // 2. Identify the client one last time if they disconnected immediately
    if (clientId.empty())
    {
        std::lock_guard<std::mutex> lock(sessionMutex);
        if (clientIdMap.count(client_fd))
        {
            clientId = clientIdMap[client_fd];
        }
    }

    // 3. TRIGGER LAST WILL (The Core Fix)
    if (!clientId.empty())
    {
        if (!gracefulDisconnect)
        {
            Logger::log(LEVEL::WARNING, "Ungraceful disconnect: %s (fd %d)", clientId.c_str(), client_fd);
            triggerLastWill(clientId);
        }
        else
        {
            Logger::log(LEVEL::INFO, "Graceful disconnect: %s (fd %d)", clientId.c_str(), client_fd);
        }

        // 4. Cleanup session and subscriptions
        cleanupSession(clientId, client_fd);
    }

    close(client_fd);
}

bool mqttbroker::processPacket(int client_fd, bool &gracefulDisconnect)
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
        uint8_t qos = 0;
        bool retain = false;
        bool dup = false;
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
            dup = (flags & 0x08) >> 3;

            // Bits 2-1: QoS Level
            // Mask with 0110 (which is 0x06) then shift right by 1
            qos = (flags & 0x06) >> 1;

            // Bit 0: Retain Flag
            retain = (flags & 0x01);

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

        // puback received (QoS 1 acknowledgement)
        case Signal::PUBACK:
            handlePuback(client_fd, buffer, index, remainingLength);
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
            Logger::log(LEVEL::DEBUG, "DISCONNECT packet received from fd %d", client_fd);
            gracefulDisconnect = true;
            return false; // Break the while loop in handleClient

        case Signal::PINGREQ:
            // Always send PINGRESP immediately
            sendPingResp(client_fd);
            break;

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

void mqttbroker::proceedToV311Checklist(int client_fd, const std::vector<uint8_t> &buffer, size_t &index)
{
    // --- 1. Extract Variable Header ---
    uint8_t connectFlags = buffer[index++];

    // Bit breakdown of connectFlags:
    bool reserved = connectFlags & 0x01; // MUST be 0
    bool cleanSession = connectFlags & 0x02;
    bool willFlag = connectFlags & 0x04;
    uint8_t willQoS = (connectFlags & 0x18) >> 3;
    bool willRetain = connectFlags & 0x20;
    bool passwordFlag = connectFlags & 0x40;
    bool usernameFlag = connectFlags & 0x80;

    if (reserved)
    {
        close(client_fd); // Protocol violation
        return;
    }

    uint16_t keepAlive = (buffer[index] << 8) | buffer[index + 1];
    index += 2;

    std::string clientId = get_string(buffer, index);

    // ADD THE CHECK HERE:
    if (clientId.empty())
    {
        clientId = "client_" + std::to_string(client_fd);
        Logger::log(LEVEL::INFO, "Empty ClientID provided. Assigned: %s", clientId.c_str());
    }
    // Optional fields (only read if flags are set)
    std::string willTopic = "";
    std::string willPayload = "";

    if (willFlag)
    {
        willTopic = get_string(buffer, index); // Standard UTF-8

        // The Payload in MQTT 3.1.1 is also prefixed by a 2-byte length
        willPayload = get_string(buffer, index);

        Logger::log(LEVEL::DEBUG, "Will configured: Topic=%s, Payload=%s",
                    willTopic.c_str(), willPayload.c_str());
    }

    std::string username = "";
    if (usernameFlag)
    {
        username = get_string(buffer, index);
        Logger::log(LEVEL::DEBUG, "Username provided: %s", username.c_str());
    }

    std::string password = "";
    if (passwordFlag)
    {
        password = get_string(buffer, index);
        Logger::log(LEVEL::DEBUG, "Password provided");
    }

    // --- 3. Handle Session Logic (The QoS/Persistence part) ---
    handleSessionLifecycle(client_fd, clientId, cleanSession, willTopic, willPayload, willQoS, willRetain, willFlag);
}
void mqttbroker::handleSessionLifecycle(int client_fd, const std::string &clientId, bool cleanSession,
                                        std::string willTopic, std::string willPayload,
                                        uint8_t willQoS, bool willRetain, bool willFlag)
{
    // 1. We keep the lock for the ENTIRE duration of the setup to prevent crashes
    std::lock_guard<std::mutex> lock(sessionMutex);

    bool sessionPresent = false;
    auto it = sessions.find(clientId);

    if (it != sessions.end()) {
        if (cleanSession) {
            sessions.erase(it);
            sessions[clientId] = Session(client_fd, true);
            sessionPresent = false;
        } else {
            it->second.socket = client_fd;
            sessionPresent = true;
        }
    } else {
        sessions[clientId] = Session(client_fd, cleanSession);
        sessionPresent = false;
    }

    // Update session data
    Session &session = sessions[clientId];
    session.willTopic = willTopic;
    session.willPayload = willPayload;
    session.willQoS = willQoS;
    session.willRetain = willRetain;
    session.willFlag = willFlag;
    session.socket = client_fd;

    clientIdMap[client_fd] = clientId;

    Logger::log(LEVEL::INFO, "Session ready for %s (fd %d)", clientId.c_str(), client_fd);

    // 2. CRITICAL: Only call networking if they DON'T lock sessionMutex again
    // If your broker hangs here, your sendConnack has a lock inside it!
    sendConnack(client_fd, sessionPresent ? 0x01 : 0x00, 0x00);
}

void mqttbroker::proceedToV50Checklist(int client_fd, const std::vector<uint8_t> &buffer, size_t &index)
{
    // TODO: Implement MQTT 5.0 support
    Logger::log(LEVEL::WARNING, "MQTT 5.0 not yet implemented");
}

void mqttbroker::sendConnack(int client_fd, uint8_t ackFlag, uint8_t returnCode)
{
    std::vector<uint8_t> packet = {
        0x20,      // Byte 1: CONNACK Header
        0x02,      // Byte 2: Remaining Length
        ackFlag,   // Byte 3: Acknowledge Flags (Session Present = 0 or 1)
        returnCode // Byte 4: The variable return code (0x00, 0x01, 0x04, etc.)
    };
    send(client_fd, (const char *)packet.data(), packet.size(), 0);
    Logger::log(LEVEL::INFO, "sent CONNACK to client %d (flags=0x%02x, code=0x%02x)", client_fd, ackFlag, returnCode);
}

void mqttbroker::logPublish(int client_fd, const std::string &topic, const std::string &message)
{
    Logger::log(LEVEL::INFO, "published message to topic '%s': %s", topic.c_str(), message.c_str());
}

void mqttbroker::forwardToSubscribers(const std::string &topic, const std::string &message, int exclude_fd)
{
    // Step 1: Collect subscriber information (needs both locks for consistency)
    struct SubscriberInfo
    {
        int socket;
        uint8_t qos;
        std::string clientId;
        uint16_t packetId;
    };
    std::vector<SubscriberInfo> subscribers;

    {
        // Lock order: sessionMutex FIRST, then subMutex (consistent lock ordering to prevent deadlock)
        std::lock_guard<std::mutex> sessLock(sessionMutex);
        std::lock_guard<std::mutex> subLock(subMutex);

        // find subscribers for matching topics
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

                // Find the clientId for this socket
                std::string clientId = "";
                auto it = clientIdMap.find(sock);
                if (it != clientIdMap.end())
                {
                    clientId = it->second;
                }

                uint16_t pid = 0;
                // Generate packet ID from session
                if (qos > 0 && !clientId.empty())
                {
                    auto session_it = sessions.find(clientId);
                    if (session_it != sessions.end())
                    {
                        pid = generatePacketId(clientId); // Safe: sessionMutex already held

                        if (qos == 2)
                        {
                            // Store for QoS 2 handshake
                            inflightOutgoing[sock][pid] = PendingQoS2{topic, message};
                        }
                    }
                }

                subscribers.push_back({sock, qos, clientId, pid});
            }
        }
    } // Release both locks before sending

    // Step 2: Send messages to each subscriber (outside locks to avoid blocking)
    for (const auto &sub : subscribers)
    {
        std::vector<uint8_t> packet;
        uint8_t headerByte = 0x30; // PUBLISH
        headerByte |= (sub.qos << 1);
        packet.push_back(headerByte);

        std::vector<uint8_t> body;

        // Topic name: [2-byte length][topic data]
        uint16_t topicLen = topic.size();
        body.push_back((topicLen >> 8) & 0xFF);
        body.push_back(topicLen & 0xFF);
        body.insert(body.end(), topic.begin(), topic.end());

        // Packet ID (only for QoS > 0)
        if (sub.qos > 0)
        {
            body.push_back((sub.packetId >> 8) & 0xFF);
            body.push_back(sub.packetId & 0xFF);
        }

        // Payload
        body.insert(body.end(), message.begin(), message.end());

        // Remaining length
        uint32_t remLen = body.size();
        if (remLen < 128)
        {
            packet.push_back(static_cast<uint8_t>(remLen));
        }
        else
        {
            // Multi-byte remaining length encoding
            std::vector<uint8_t> lenBytes;
            while (remLen > 0)
            {
                uint8_t byte = remLen & 0x7F;
                remLen >>= 7;
                if (remLen > 0)
                {
                    byte |= 0x80;
                }
                lenBytes.push_back(byte);
            }
            packet.insert(packet.end(), lenBytes.begin(), lenBytes.end());
        }

        // Append body
        packet.insert(packet.end(), body.begin(), body.end());

        // Send to subscriber
        send(sub.socket, (const char *)packet.data(), packet.size(), 0);
        Logger::log(LEVEL::DEBUG, "Forwarded message to socket %d: topic=%s, qos=%u, packetId=%u",
                    sub.socket, topic.c_str(), sub.qos, sub.packetId);
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
void mqttbroker::handleSubscribe(int client_fd, const std::vector<uint8_t> &buffer, size_t &index, uint32_t remainingLength)
{
    // The index is already pointing at the Packet ID because the main loop
    // skipped the Fixed Header and decoded the Variable Length.
    uint16_t packetId = (buffer[index] << 8) | buffer[index + 1];
    index += 2;

    std::vector<uint8_t> returnCodes;
    size_t endOfPacket = index + (remainingLength - 2);

    while (index < endOfPacket)
    {
        std::string topic = get_string(buffer, index); // index moves automatically
        uint8_t qos = buffer[index++];

        {
            std::lock_guard<std::mutex> lock(subMutex);
            topicSubscribers[topic][client_fd] = qos;

            // Also update the session so we can clean up on disconnect!
            std::string cid = clientIdMap[client_fd];
            sessions[cid].subscriptions.insert(topic);
        }

        Logger::log(LEVEL::INFO, "Client %d subscribed to '%s' (QoS %u)", client_fd, topic.c_str(), qos);
        returnCodes.push_back(qos);
    }

    // Build SUBACK (Using your existing logic, it's correct!)
    std::vector<uint8_t> suback = {0x90, (uint8_t)(2 + returnCodes.size())};
    suback.push_back(static_cast<uint8_t>(packetId >> 8));
    suback.push_back(static_cast<uint8_t>(packetId & 0xFF));
    suback.insert(suback.end(), returnCodes.begin(), returnCodes.end());

    send(client_fd, suback.data(), suback.size(), 0);
}

void mqttbroker::handleUnsubscribe(int client_fd, const std::vector<uint8_t> &buffer, size_t index, uint32_t remainingLength)
{
    // TODO: Implement unsubscribe handling
    Logger::log(LEVEL::DEBUG, "handleUnsubscribe not yet implemented");
}

// ============================================================================
// PACKET ID FACTORY - Helper function to generate unique packet IDs per session
// NOTE: Caller MUST hold sessionMutex lock to prevent deadlock!
// ============================================================================
uint16_t mqttbroker::generatePacketId(const std::string &clientId)
{
    // DO NOT LOCK HERE - sessionMutex must already be held by caller
    // Locking here would cause deadlock when called from resumeDelayedMessages

    auto it = sessions.find(clientId);
    if (it == sessions.end())
    {
        Logger::log(LEVEL::ERROR, "generatePacketId: Session not found for clientId: %s", clientId.c_str());
        return 1;
    }

    // Increment the packet ID
    it->second.nextPacketId++;

    // Wrap around from 65535 to 1 (never use 0)
    if (it->second.nextPacketId == 0)
    {
        it->second.nextPacketId = 1;
    }

    Logger::log(LEVEL::DEBUG, "Generated PacketId %u for clientId: %s", it->second.nextPacketId, clientId.c_str());
    return it->second.nextPacketId;
}

// ============================================================================
// SEND PUBLISH TO CLIENT - Helper function to consolidate publish sending logic
// ============================================================================
void mqttbroker::sendPublishToClient(int client_fd, const MQTTMessage &msg, uint16_t packetId, bool dup)
{
    std::vector<uint8_t> packet;

    // Build Fixed Header
    uint8_t headerByte = 0x30; // PUBLISH control packet type

    // Set DUP flag if retransmitting
    if (dup)
    {
        headerByte |= 0x08;
    }

    // Set QoS bits
    headerByte |= (msg.qos << 1);

    // Set RETAIN flag
    if (msg.retain)
    {
        headerByte |= 0x01;
    }

    packet.push_back(headerByte);

    // Build Variable Header + Payload
    std::vector<uint8_t> body;

    // Topic name - each string is encoded as [2-byte length][string data]
    uint16_t topicLen = msg.topic.size();
    body.push_back((topicLen >> 8) & 0xFF);
    body.push_back(topicLen & 0xFF);
    body.insert(body.end(), msg.topic.begin(), msg.topic.end());

    // Packet Identifier (only for QoS > 0)
    if (msg.qos > 0)
    {
        body.push_back((packetId >> 8) & 0xFF);
        body.push_back(packetId & 0xFF);
    }

    // Payload
    body.insert(body.end(), msg.payload.begin(), msg.payload.end());

    // Remaining Length
    uint32_t remainingLength = body.size();
    if (remainingLength < 128)
    {
        packet.push_back(static_cast<uint8_t>(remainingLength));
    }
    else
    {
        // Handle multi-byte remaining length
        std::vector<uint8_t> lenBytes;
        while (remainingLength > 0)
        {
            uint8_t byte = remainingLength & 0x7F;
            remainingLength >>= 7;
            if (remainingLength > 0)
            {
                byte |= 0x80;
            }
            lenBytes.push_back(byte);
        }
        packet.insert(packet.end(), lenBytes.begin(), lenBytes.end());
    }

    // Append body
    packet.insert(packet.end(), body.begin(), body.end());

    // Send to client
    send(client_fd, (const char *)packet.data(), packet.size(), 0);
    Logger::log(LEVEL::DEBUG, "Sent PUBLISH to client %d: topic=%s, qos=%u, dup=%d, packetId=%u",
                client_fd, msg.topic.c_str(), msg.qos, dup, packetId);
}

// Resume queued messages on reconnect

void mqttbroker::resumeDelayedMessages(const std::string &clientId, int client_fd)
{
    std::lock_guard<std::mutex> lock(sessionMutex);

    auto it = sessions.find(clientId);
    if (it == sessions.end())
    {
        Logger::log(LEVEL::WARNING, "resumeDelayedMessages: Session not found for clientId: %s", clientId.c_str());
        return;
    }

    Session &session = it->second;

    Logger::log(LEVEL::INFO, "Resuming delayed messages for clientId: %s (offline queue size: %zu, inflight size: %zu)",
                clientId.c_str(), session.offlineQueue.size(), session.inflight.size());

    // 1. Process offline queue (messages that arrived while disconnected)
    while (!session.offlineQueue.empty())
    {
        MQTTMessage msg = session.offlineQueue.front();
        session.offlineQueue.pop();

        if (msg.qos == 0)
        {
            // QoS 0: Send and forget
            sendPublishToClient(client_fd, msg, 0, false);
        }
        else
        {
            // QoS 1/2: Assign PacketId, move to inflight, and set appropriate status
            uint16_t pid = generatePacketId(clientId);
            msg.packetId = pid;
            msg.status = (msg.qos == 1) ? MessageStatus::AWAITING_PUBACK : MessageStatus::AWAITING_PUBREC;
            msg.timestamp = std::time(nullptr); // Update timestamp

            session.inflight[pid] = msg;
            sendPublishToClient(client_fd, msg, pid, false);

            Logger::log(LEVEL::DEBUG, "Resumed message from offline queue: packetId=%u, qos=%u, status=%d",
                        pid, msg.qos, static_cast<int>(msg.status));
        }
    }

    // 2. Retry inflight messages (messages sent before disconnect that never got ACK)
    for (auto &[pid, msg] : session.inflight)
    {
        // Update timestamp to current time for retry tracking
        msg.timestamp = std::time(nullptr);

        // Resend with DUP flag set to 1 (indicates this is a retransmission)
        sendPublishToClient(client_fd, msg, pid, true);

        Logger::log(LEVEL::DEBUG, "Resending inflight message: packetId=%u, qos=%u, dup=true", pid, msg.qos);
    }
}

void mqttbroker::handlePublish(int client_fd, const std::vector<uint8_t> &buffer, size_t index, uint32_t remainingLength, uint8_t qos, bool retain, bool dup)
{
    // Parse variable header and payload
    size_t startIndex = index;

    // Topic name
    std::string topic = get_string(buffer, index);

    uint16_t packetId = 0;
    if (qos > 0)
    {
        packetId = get_uint16(buffer, index);
        index += 2;
    }

    // Payload is everything remaining
    size_t payloadSize = (startIndex + remainingLength) - index;
    std::string payload(buffer.begin() + index, buffer.begin() + index + payloadSize);

    Logger::log(LEVEL::INFO, "Received PUBLISH from client %d: topic=%s, qos=%u, retain=%d, dup=%d, packetId=%u",
                client_fd, topic.c_str(), qos, retain, dup, packetId);

    logPublish(client_fd, topic, payload);

    // Handle QoS 2  - Must wait for PUBREL before delivering
    if (qos == 2)
    {
        // Store message until we receive PUBREL
        {
            std::lock_guard<std::mutex> lock(subMutex);
            inflightIncoming[client_fd][packetId] = PendingQoS2{topic, payload};
        }

        // Send PUBREC back to client
        std::vector<uint8_t> pubrec;
        pubrec.push_back(PUBREC_TYPE);
        pubrec.push_back(0x02); // Remaining length
        pubrec.push_back((packetId >> 8) & 0xFF);
        pubrec.push_back(packetId & 0xFF);
        send(client_fd, pubrec.data(), pubrec.size(), 0);

        Logger::log(LEVEL::DEBUG, "Sent PUBREC to client %d for packetId %u", client_fd, packetId);
        return;
    }

    // For QoS 0 and 1, forward to subscribers immediately
    forwardToSubscribers(topic, payload, client_fd);

    // Send PUBACK for QoS 1
    if (qos == 1)
    {
        std::vector<uint8_t> puback;
        puback.push_back(PUBACK_TYPE);
        puback.push_back(0x02); // Remaining length
        puback.push_back((packetId >> 8) & 0xFF);
        puback.push_back(packetId & 0xFF);
        send(client_fd, puback.data(), puback.size(), 0);

        Logger::log(LEVEL::DEBUG, "Sent PUBACK to client %d for packetId %u", client_fd, packetId);
    }
}

// ============================================================================
// HANDLE PUBACK - QoS 1 acknowledgement
// ============================================================================
void mqttbroker::handlePuback(int client_fd, const std::vector<uint8_t> &buffer, size_t index, uint32_t remainingLength)
{
    if (remainingLength < 2)
    {
        Logger::log(LEVEL::WARNING, "PUBACK: Invalid remaining length %u", remainingLength);
        return;
    }

    uint16_t packetId = get_uint16(buffer, index);

    Logger::log(LEVEL::DEBUG, "Received PUBACK from client %d for packetId %u", client_fd, packetId);

    // Find which client ID this socket belongs to
    std::string clientId;
    {
        std::lock_guard<std::mutex> lock(sessionMutex);
        auto it = clientIdMap.find(client_fd);
        if (it == clientIdMap.end())
        {
            Logger::log(LEVEL::WARNING, "PUBACK: Unknown client fd %d", client_fd);
            return;
        }
        clientId = it->second;
    }

    // Find and remove the message from inflight map
    {
        std::lock_guard<std::mutex> lock(sessionMutex);
        auto session_it = sessions.find(clientId);
        if (session_it != sessions.end())
        {
            auto inflight_it = session_it->second.inflight.find(packetId);
            if (inflight_it != session_it->second.inflight.end())
            {
                Logger::log(LEVEL::INFO, "Message PacketId %u confirmed delivered to client %d", packetId, client_fd);
                session_it->second.inflight.erase(inflight_it);
            }
            else
            {
                Logger::log(LEVEL::WARNING, "PUBACK: PacketId %u not found in inflight map for clientId %s", packetId, clientId.c_str());
            }
        }
    }
}

// ============================================================================
// HANDLE PUBREC - QoS 2 first step (broker receives from client)
// ============================================================================
void mqttbroker::handlePubrec(int client_fd, const std::vector<uint8_t> &buffer, size_t index, uint32_t remainingLength)
{
    if (remainingLength < 2)
    {
        Logger::log(LEVEL::WARNING, "PUBREC: Invalid remaining length %u", remainingLength);
        return;
    }

    uint16_t packetId = get_uint16(buffer, index);
    Logger::log(LEVEL::DEBUG, "Received PUBREC from client %d for packetId %u", client_fd, packetId);

    // Find the clientId for this socket
    std::string clientId;
    {
        std::lock_guard<std::mutex> lock(sessionMutex);
        auto it = clientIdMap.find(client_fd);
        if (it == clientIdMap.end())
        {
            Logger::log(LEVEL::WARNING, "PUBREC: Unknown client fd %d", client_fd);
            return;
        }
        clientId = it->second;
    }

    // Update message status from AWAITING_PUBREC to AWAITING_PUBCOMP
    {
        std::lock_guard<std::mutex> lock(sessionMutex);
        auto session_it = sessions.find(clientId);
        if (session_it != sessions.end())
        {
            auto inflight_it = session_it->second.inflight.find(packetId);
            if (inflight_it != session_it->second.inflight.end())
            {
                inflight_it->second.status = MessageStatus::AWAITING_PUBCOMP;
                Logger::log(LEVEL::DEBUG, "Updated message status to AWAITING_PUBCOMP for packetId %u", packetId);
            }
        }
    }

    // Send PUBREL back to client
    std::vector<uint8_t> pubrel;
    pubrel.push_back(PUBREL_TYPE); // 0x62 = PUBREL with flags 0b0010
    pubrel.push_back(0x02);        // Remaining length
    pubrel.push_back((packetId >> 8) & 0xFF);
    pubrel.push_back(packetId & 0xFF);
    send(client_fd, pubrel.data(), pubrel.size(), 0);

    Logger::log(LEVEL::DEBUG, "Sent PUBREL to client %d for packetId %u", client_fd, packetId);
}

// ============================================================================
// HANDLE PUBREL - QoS 2 second step (broker sends to client)
// ============================================================================
void mqttbroker::handlePubrel(int client_fd, const std::vector<uint8_t> &buffer, size_t index, uint32_t remainingLength)
{
    if (remainingLength < 2)
    {
        Logger::log(LEVEL::WARNING, "PUBREL: Invalid remaining length %u", remainingLength);
        return;
    }

    uint16_t packetId = get_uint16(buffer, index);
    Logger::log(LEVEL::DEBUG, "Received PUBREL from client %d for packetId %u", client_fd, packetId);

    // Extract the pending message and deliver it
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
                Logger::log(LEVEL::DEBUG, "Found pending QoS2 message for packetId %u", packetId);
            }
        }
    }

    // Forward to subscribers
    if (!pending.topic.empty())
    {
        forwardToSubscribers(pending.topic, pending.payload, client_fd);
    }

    // Send PUBCOMP back to client
    std::vector<uint8_t> pubcomp;
    pubcomp.push_back(PUBCOMP_TYPE); // 0x70 = PUBCOMP
    pubcomp.push_back(0x02);         // Remaining length
    pubcomp.push_back((packetId >> 8) & 0xFF);
    pubcomp.push_back(packetId & 0xFF);
    send(client_fd, pubcomp.data(), pubcomp.size(), 0);

    Logger::log(LEVEL::DEBUG, "Sent PUBCOMP to client %d for packetId %u", client_fd, packetId);
}

// ============================================================================
// HANDLE PUBCOMP - QoS 2 completion (client confirms receipt of PUBREL)
// ============================================================================
void mqttbroker::handlePubcomp(int client_fd, const std::vector<uint8_t> &buffer, size_t index, uint32_t remainingLength)
{
    if (remainingLength < 2)
    {
        Logger::log(LEVEL::WARNING, "PUBCOMP: Invalid remaining length %u", remainingLength);
        return;
    }

    uint16_t packetId = get_uint16(buffer, index);
    Logger::log(LEVEL::DEBUG, "Received PUBCOMP from client %d for packetId %u", client_fd, packetId);

    // Find the clientId for this socket
    std::string clientId;
    {
        std::lock_guard<std::mutex> lock(sessionMutex);
        auto it = clientIdMap.find(client_fd);
        if (it == clientIdMap.end())
        {
            Logger::log(LEVEL::WARNING, "PUBCOMP: Unknown client fd %d", client_fd);
            return;
        }
        clientId = it->second;
    }

    // Remove message from inflight map - QoS 2 handshake is complete
    {
        std::lock_guard<std::mutex> lock(sessionMutex);
        auto session_it = sessions.find(clientId);
        if (session_it != sessions.end())
        {
            auto inflight_it = session_it->second.inflight.find(packetId);
            if (inflight_it != session_it->second.inflight.end())
            {
                Logger::log(LEVEL::INFO, "QoS 2 handshake complete for packetId %u, removing from inflight", packetId);
                session_it->second.inflight.erase(inflight_it);
            }
        }
    }
}

// ============================================================================
// SEND PING RESPONSE
// ============================================================================
void mqttbroker::sendPingResp(int client_fd)
{
    std::vector<uint8_t> pingresp;
    pingresp.push_back(0xD0); // PINGRESP fixed header
    pingresp.push_back(0x00); // Remaining length = 0
    send(client_fd, pingresp.data(), pingresp.size(), 0);
    Logger::log(LEVEL::DEBUG, "Sent PINGRESP to client %d", client_fd);
}

// ============================================================================
// INTERNAL PUBLISH - For publishing will messages and system messages
// ============================================================================
void mqttbroker::internalPublish(const std::string &topic, const std::string &payload, uint8_t qos, bool retain)
{
    Logger::log(LEVEL::INFO, "Internal publish to topic '%s': %s (QoS=%u)", topic.c_str(), payload.c_str(), qos);

    MQTTMessage msg(topic, payload, qos, retain);
    forwardToSubscribers(topic, payload, -1); // -1 means don't exclude any client
}

// ============================================================================
// TRIGGER LAST WILL - Called on ungraceful client disconnect
// ============================================================================
void mqttbroker::triggerLastWill(const std::string &clientId) {
    std::string topic;
    std::string payload;
    uint8_t qos;
    bool retain;
    bool shouldPublish = false;

    {
        std::lock_guard<std::mutex> lock(sessionMutex);
        auto it = sessions.find(clientId);
        if (it != sessions.end() && it->second.willFlag) {
            topic = it->second.willTopic;
            payload = it->second.willPayload;
            qos = it->second.willQoS;
            retain = it->second.willRetain;
            shouldPublish = true;
            it->second.willFlag = false; // Clear it so it only fires once
        }
    } // <--- sessionMutex is RELEASED here

    if (shouldPublish && !topic.empty()) {
        Logger::log(LEVEL::INFO, "Publishing Last Will for %s", clientId.c_str());
        internalPublish(topic, payload, qos, retain); // Now this can safely lock!
    }
}

// ============================================================================
// RETRY INFLIGHT MESSAGES - Background thread to retry unacknowledged messages
// ============================================================================
void mqttbroker::retryInflightMessages()
{
    const int RETRY_TIMEOUT_SECONDS = 20; // Retry messages older than 20 seconds

    while (true)
    {
        std::this_thread::sleep_for(std::chrono::seconds(5)); // Check every 5 seconds

        std::time_t now = std::time(nullptr);

        std::lock_guard<std::mutex> lock(sessionMutex);

        for (auto &[clientId, session] : sessions)
        {
            if (session.socket < 0)
            {
                continue; // Skip disconnected sessions
            }

            // Check inflight messages for timeout
            for (auto &[packetId, msg] : session.inflight)
            {
                if (now - msg.timestamp >= RETRY_TIMEOUT_SECONDS)
                {
                    Logger::log(LEVEL::INFO, "Retrying message packetId %u for client %s (timeout: %ld seconds)",
                                packetId, clientId.c_str(), now - msg.timestamp);

                    // Resend with DUP flag
                    sendPublishToClient(session.socket, msg, packetId, true);

                    // Update timestamp for next retry
                    msg.timestamp = now;
                }
            }
        }
    }
}

void mqttbroker::cleanupSession(const std::string &clientId, int client_fd)
{
    // Lock both to ensure total consistency during cleanup
    std::lock_guard<std::mutex> sessLock(sessionMutex);
    std::lock_guard<std::mutex> subLock(subMutex);

    auto sessIt = sessions.find(clientId);
    if (sessIt != sessions.end())
    {
        // 1. Remove this specific socket from all topic subscriber lists
        for (const std::string &topic : sessIt->second.subscriptions)
        {
            if (topicSubscribers.count(topic))
            {
                topicSubscribers[topic].erase(client_fd);
            }
        }

        // 2. Handle CleanSession logic
        if (sessIt->second.cleanSession)
        {
            // MQTT Spec: Wipe everything for CleanSession=true
            sessions.erase(sessIt);
            Logger::log(LEVEL::DEBUG, "CleanSession: Session data wiped for %s", clientId.c_str());
        }
        else
        {
            // MQTT Spec: Keep subscriptions, but mark socket as dead (-1)
            sessIt->second.socket = -1;
            Logger::log(LEVEL::DEBUG, "Persistent Session: State saved for %s", clientId.c_str());
        }
    }

    // 3. Remove the FD mapping as the socket is about to be closed
    clientIdMap.erase(client_fd);
}