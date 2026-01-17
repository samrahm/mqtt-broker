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
}

void mqttbroker::publishMessage(const std::string& topic, const std::string& message) {
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

void mqttbroker::addSubscription(const std::string& clientId, const std::string& topic) {
    subscriptions.push_back({clientId, topic});
    std::cout << "Client " << clientId << " subscribed to topic: " << topic << std::endl;
}

bool mqttbroker::isRunning() const {
    return running;
}



void mqttbroker::sendConnack(int client_fd)
{
}

void mqttbroker::logPublish(int, const std::string &topic, const std::string &message)
{
    cout << "published message to topic '" << topic.c_str() << "': " message.c_str() << endl;;
}