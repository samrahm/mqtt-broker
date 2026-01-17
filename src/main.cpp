#include <iostream>
#include "include/mqttbroker.h"

int main()
{
    // load the json config
    BrokerConfig config = JSONParser::loadConfig("config.json");

    // create broker instance

    cout << "MQTT Broker starting..." << endl;
    cout << "Port: " << config.port << endl;
    cout << "Max Clients: " << config.maxClients << endl;

    mqttbroker broker(config.port);
    mqttbroker.start();

    return 0;
}