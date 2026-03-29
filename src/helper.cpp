#include <cstring>
#include <vector>
#include <cstdint>
#include <stdexcept>
#include <unistd.h>

uint32_t decodeVarint(const std::vector<uint8_t> &stream, size_t &index)
{
    uint32_t multiplier = 1;
    uint32_t value = 0;
    uint8_t encodedByte;

    do
    {
        if (index >= stream.size())
        {
            throw std::runtime_error("Unexpected end of stream");
        }

        encodedByte = stream[index++];

        // add the lower 7 bits to the value
        value += (encodedByte & 127) * multiplier;

        // security check for 4-byte limit (as per your logic)
        if (multiplier > 128 * 128 * 128)
        {
            throw std::runtime_error("Malformed Variable Byte Integer: Too many bytes");
        }

        multiplier *= 128;

    } while ((encodedByte & 128) != 0); // continue if the continuation bit (msb) is 1

    return value;
}

bool isValidFlags(uint8_t type, uint8_t flags)
{
    switch (type)
    {
    case 1:
        return flags == 0x00; // CONNECT
    case 3:
        return true; // PUBLISH (All flags 0-15 are valid)
    case 4:
        return flags == 0x02; // PUBREL
    case 8:
        return flags == 0x02; // SUBSCRIBE
    case 10:
        return flags == 0x02; // UNSUBSCRIBE
    case 12:
        return flags == 0x00; // PINGREQ
    case 13:
        return flags == 0x00; // PINGRESP
    case 14:
        return flags == 0x00; // DISCONNECT
    default:
        return (flags == 0x00); // Most others (CONNACK, PUBACK, etc.)
    }
}

std::string get_string(const std::vector<uint8_t> &buffer, size_t &offset)
{
    if (offset + 2 > buffer.size())
        return ""; // safety check

    uint16_t len = (buffer[offset] << 8) | buffer[offset + 1];

    if (offset + 2 + len > buffer.size())
        return ""; // safety check

    std::string str(buffer.begin() + offset + 2, buffer.begin() + offset + 2 + len);
    offset += (2 + len);
    return str;
}