#include <iostream>
#include "include/mqttbroker.h"
#include "include/json_parser.h"

int main()
{
    // load the json config
    BrokerConfig config = JSONParser::loadConfig("config.json");

    // create broker instance

    std::cout << "MQTT Broker starting..." << std::endl;
    std::cout << "Port: " << config.port << std::endl;
    std::cout << "Max Clients: " << config.maxClients << std::endl;

    mqttbroker broker(config.port);
    mqttbroker.start();

    return 0;
}