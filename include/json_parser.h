#pragma once
#include <nlohmann/json.hpp>
#include <fstream>
#include <iostream>
using namespace std;

using json = nlohmann::json;

struct BrokerConfig {
    int port ;
    int max_clients ;
    std::string log_level ;
};

class JSONParser {
public:
    static BrokerConfig loadConfig(const std::string& path);
};