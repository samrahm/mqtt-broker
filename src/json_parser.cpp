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
    if(j.contains("max_clients")) cfg.max_clients = j["max_clients"];
    if(j.contains("log_level")) cfg.log_level = j["log_level"];

    return cfg;
}