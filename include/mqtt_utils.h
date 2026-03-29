// mqtt_utils.hpp
#pragma once

#include <cstdint>
#include <vector>
#include <cstring>
#include <string>

inline uint16_t get_uint16(const std::vector<uint8_t> &data, size_t offset)
{
    return (static_cast<uint16_t>(data[offset]) << 8) | data[offset + 1];
}

inline uint8_t get_uint8(const std::vector<uint8_t> &data, size_t offset)
{
    return data[offset];
}

inline void put_uint16(std::vector<uint8_t> &data, size_t offset, uint16_t value)
{
    data[offset] = static_cast<uint8_t>((value >> 8) & 0xFF);
    data[offset + 1] = static_cast<uint8_t>(value & 0xFF);
}

inline void put_string(std::vector<uint8_t> &data, const std::string &str)
{
    data.push_back((str.size() >> 8) & 0xFF);
    data.push_back(str.size() & 0xFF);
    data.insert(data.end(), str.begin(), str.end());
}

// inline std::string get_string(const std::vector<uint8_t> &data, size_t &offset)
// {
//     uint16_t length = get_uint16(data, offset);
//     offset += 2;
//     std::string result(data.begin() + offset, data.begin() + offset + length);
//     offset += length;
//     return result;
// }

struct RemainingLengthResult
{
    uint32_t value;
    uint8_t bytesUsed;
};

inline RemainingLengthResult parseRemainingLength(const std::vector<uint8_t> &data)
{
    uint32_t value = 0;
    uint32_t multiplier = 1;
    uint8_t bytesUsed = 0;
    size_t index = 1; // after fixed header

    while (true)
    {
        uint8_t encodedByte = data[index++];
        bytesUsed++;
        value += (encodedByte & 0x7F) * multiplier;
        multiplier *= 128;

        if (!(encodedByte & 0x80))
            break;
        if (bytesUsed > 4)
            throw std::runtime_error("Malformed Remaining Length");
    }

    return {value, bytesUsed};
}
