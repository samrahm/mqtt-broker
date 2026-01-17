#include <cstring>
#include <iostream>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

mqttbroker::mqttbroker(int port) : port(port), serversock(-1) {}

void mqttbroker::start()
{
    serversock = socket(AF_INET, SOCK_STREAM, 0);
    if (serversock == 0)
    {
        cerr << "socket failed\n";
        return 1;
    }

    sockaddr_in address;
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(8080);

    errorcheck(bind(server_fd, (struct sockaddr *)&address, sizeof(address)), "failed to bind to port 8080.\n");
    errorcheck(listen(server_fd, 3), "failed to listen in the socket.\n");

    while (true)
    {
        int addrlen = sizeof(address);
        int client_fd = accept(server_fd, (struct sockaddr *)&address, (socklen_t *)&addrlen);
        if (client_fd < 0)
        {
            cerr << "failed to accept\n";
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
        cout << "mqtt disconnected." << endl;
    }
}

// client handling
void mqttbroker::handleClient(int client_fd)
{
    while (true)
    {
        if (!processPacket(client_fd))
        {
            cout << "client disconnected." << endl;
            break;
        }
    }
    close(client_fd);
}

void mqttbroker::processPacket(int client_fd)
{
    cout << "processing packet" << endl;
    // read packet
    std::vector<uint8_t> buffer(1024);
    int bytes = recv(client_fd, buffer.data(), buffer.size(), 0);
    if (bytes <= 0)
    {
        if (bytes < 0)  
        {
            cout << "client disconnected or error " << endl;
            cout << "receive failed on socket " << client_fd << endl;
        }
        buffer.clear();
        close(client_fd);
        return false;
    }

    buffer.resize();

    // decide whether conn, publish or subs
    
    uint8_t packetType = buffer[0] >> 4;    // left shift to keep upper 4 bits containing packet type 
    switch (static_cast<Signal>(packetType))
    {
    case Signal::CONNECT:
        cout << "Client connected " << client_fd << endl;
        sendConnack(client_fd);
        break;
    case Signal::PUBLISH:
        handlePublish(client_fd, buffer, bytes);
        break;
    case Signal::SUBSCRIBE:
        handleSubscribe(client_fd, buffer);
        break;
    case Signal::DISCONNECT:
        close(client_fd);     
        cout << "Client disconnected " << client_fd << endl;
        return false;
    default:
        cout << "unsupported packet type : " << packetType << endl;
        return false;
    }
    return true;
}

void mqttbroker::sendConnack(int client_fd)
{
    uint8_t connack[4] = {0x20, 0x02, 0x00, 0x00}; 
    send(client_fd, connack, sizeof(connack), 0);
    Logger::log(LEVEL::INFO, "sent CONNACK to client %s \n",  clientSock );
}

// void mqttbroker::handlePublish(int client_fd, const std::vector<uint8_t> &buffer, int byt);
void mqttbroker::handlePublish(const std::string& topic, const std::string& message) {
    std::cout << "publishing message to topic '" << topic << "': " << message << std::endl;
    
    // Store message
    topicMessages[topic].push_back(message);
    
    // TODO: Deliver to subscribers
    for (const auto& sub : subscriptions) {
        if (sub.topic == topic) {
            std::cout << "  -> Delivering to client: " << sub.clientId << std::endl;
        }
    }
}

void mqttbroker::logPublish(int, const std::string &topic, const std::string &message)
{
    Logger::log(LEVEL::INFO, "Published message to topic '%s': %s", topic.c_str(), message.c_str());
}


void mqttbroker::handleSubscribe(int client_fd)

void mqttbroker::addSubscription(const std::string& clientId, const std::string& topic) {
    subscriptions.push_back({clientId, topic});
    std::cout << "Client " << clientId << " subscribed to topic: " << topic << std::endl;
}


void mqttbroker::logPublish(int, const std::string &topic, const std::string &message)
{
    cout << "published message to topic '" << topic.c_str() << "': " message.c_str() << endl;;
}