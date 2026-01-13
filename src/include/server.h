#ifndef SERVER_H
#define SERVER_H

#include <string>
#include <vector>
#include <map>
#include <memory>

class Server {
private:
    int serverSocket;
    int port;
    bool running;
    
    struct Subscription {
        std::string clientId;
        std::string topic;
    };
    
    std::vector<Subscription> subscriptions;
    std::map<std::string, std::vector<std::string>> topicMessages;

public:
    Server(int port);
    ~Server();
    
    bool start();
    void stop();
    void run();
    
    void handleClient(int clientSocket);
    void publishMessage(const std::string& topic, const std::string& message);
    void addSubscription(const std::string& clientId, const std::string& topic);
    
    bool isRunning() const;
};

#endif // SERVER_H
