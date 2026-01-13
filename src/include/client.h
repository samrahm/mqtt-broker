#ifndef CLIENT_H
#define CLIENT_H

#include <string>
#include <functional>

class Client {
private:
    std::string clientId;
    int socket;
    bool connected;

public:
    Client(const std::string& id);
    ~Client();
    
    bool connect(const std::string& host, int port);
    void disconnect();
    bool publish(const std::string& topic, const std::string& message);
    bool subscribe(const std::string& topic, std::function<void(const std::string&)> callback);
    bool isConnected() const;
    
    const std::string& getClientId() const;
};

#endif // CLIENT_H
