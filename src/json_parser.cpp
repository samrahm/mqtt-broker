#include "json_parser.h"

BrokerConfig JSONParser::loadConfig(const std::string& path){
    BrokerConfig cfg;

    std::ifstream file(path);
    if(!file){
        cout << "error opening the file." << endl;
        return cfg;
    }

    json j;
    file >> j;

    if(j.contains("port")) cfg.port = j["port"];
    if(j.contains("maxClients")) cfg.maxClients = j["maxClients"];
    if(j.contains("logLevel")) cfg.logLevel = j["logLevel"];

    return cfg;
}