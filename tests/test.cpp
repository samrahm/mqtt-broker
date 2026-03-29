#include "include/server.h"
#include "include/client.h"
#include <iostream>

int main()
{
    std::cout << "Testing MQTT Broker " << std::endl;

    // create and start broker
    Server broker(1883);

    if (!broker.start())
    {
        std::cerr << "Failed to start broker" << std::endl;
        return 1;
    }

    // example: create test client
    Client testClient("client-001");
    // connect to broker
    testClient.connect("localhost", 1883);

    // example: simulate some operations
    broker.addSubscription("client-001", "test/topic");
    broker.publishMessage("test/topic", "Hello MQTT!");

    testClient.disconnect();
    broker.stop();

    return 0;
}
