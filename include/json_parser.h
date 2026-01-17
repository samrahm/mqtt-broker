#pragma once
#include <nlohmann/json.hpp>
#include <fstream>
#include <iostream>
using namespace std;

using json = nlohmann::json;

struct BrokerConfig {
    int port ;
    int maxClients ;
    std::string logLevel ;
};

class JSONParser {
public:
    static BrokerConfig loadConfig(const std::string& path);
};