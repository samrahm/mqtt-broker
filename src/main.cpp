#include <iostream>
#include "mqttbroker.h"
#include "json_parser.h"

int main()
{
    // load the json config
    BrokerConfig config = JSONParser::loadConfig("config/config.json");

    // create broker instance

    std::cout << "MQTT Broker starting..." << std::endl;
    std::cout << "Port: " << config.port << std::endl;
    std::cout << "Max Clients: " << config.max_clients << std::endl;

    mqttbroker broker(config.port);
    broker.start();

    return 0;
}