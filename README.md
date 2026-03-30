# MQTT Broker

A lightweight, multi-threaded MQTT broker implementation in C++ with full protocol support for IoT applications and real-time messaging.

## Overview

This project implements a functional MQTT broker that handles client connections, message publishing, and topic subscriptions. Built with C++17 and designed for concurrent client handling, the broker loads configuration from JSON and supports core MQTT protocol operations.

## Features

- **MQTT Protocol Support**: Full implementation of MQTT signals including CONNECT, PUBLISH, SUBSCRIBE, DISCONNECT, PINGREQ, and more
- **Multi-threaded Architecture**: Thread-safe concurrent client handling using POSIX threads
- **Topic-based Routing**: Message routing with topic subscriptions and wildcard matching support
- **Session Management**: Persistent client sessions with clean session flags and state tracking
- **JSON Configuration**: Flexible broker configuration through `config.json`
- **Logging System**: Built-in logging and monitoring with configurable log levels
- **Quality of Service**: Support for QoS levels in message delivery
- **Keep-Alive**: PING request/response mechanism for connection health monitoring

## Project Structure

```
├── include/                  # Header files
│   ├── mqttbroker.h         # Main broker class and structures
│   ├── mqtt_utils.h         # MQTT utility functions and constants
│   ├── json_parser.h        # JSON configuration parser
│   ├── logger.h             # Logging utilities
│   └── mqtt_utils.h         # MQTT protocol utilities
├── src/                     # Source implementation
│   ├── main.cpp             # Entry point and broker initialization
│   ├── mqttbroker.cpp       # Broker implementation
│   ├── json_parser.cpp      # JSON config loading and parsing
│   └── helper.cpp           # Helper functions
├── config/
│   └── config.json          # Broker configuration file
├── tests/
│   └── test.cpp             # Test suite
├── Makefile                 # Build configuration
├── will_test.sh             # Will message testing script
└── stress_test.sh           # Performance and stress testing script
```

## Configuration

Configure the broker by editing `config/config.json`:

```json
{
  "port": 1884,
  "max_clients": 10,
  "log_level": "info"
}
```

| Parameter       | Description                                  | Default |
| --------------- | -------------------------------------------- | ------- |
| **port**        | TCP port for broker to listen on             | 1884    |
| **max_clients** | Maximum concurrent client connections        | 10      |
| **log_level**   | Logging verbosity (debug, info, warn, error) | info    |

## Prerequisites

- C++17 compatible compiler (GCC 7.0 or later recommended)
- POSIX-compliant system (Linux, macOS, WSL)
- Make build system
- POSIX threading support (pthread)

## Build

Build the broker executable:

```bash
make
```

This compiles all source files with optimizations (-O2) and debugging symbols (-g).

### Build Targets

- `make` or `make all` - Build the broker executable
- `make clean` - Remove compiled objects and executable
- `make run` - Build and run the broker

## Run

Start the broker with:

```bash
make run
```

Or run directly:

```bash
./mqtt-broker
```

The broker will:

- Load configuration from `config/config.json`
- Display startup information (port and max clients)
- Begin listening for incoming MQTT client connections
- Log activity based on the configured log level

## Testing

Run the test suite:

```bash
./test.cpp
```

Test specific functionality:

- **Will Messages**: `./will_test.sh` - Tests last-will-and-testament functionality
- **Stress Testing**: `./stress_test.sh` - Performance testing with multiple concurrent clients

## Usage

Once running, MQTT clients can:

- **Connect** - Establish a connection with a client ID and optional credentials
- **Publish** - Send messages to topics with configurable QoS levels
- **Subscribe** - Register for messages on specific topics or use wildcards
- **Disconnect** - Gracefully close connection; trigger will message if configured
- **Keep-Alive** - Send periodic PING requests to maintain connection stability

### Client Connection Example

Clients typically connect to the broker on the configured port (default: 1884):

```
mqtt://localhost:1884
```

## Thread Safety

The broker uses mutex-protected data structures for thread-safe operations:

- Client connection management
- Topic subscriptions
- Message queue handling
- Session state tracking

## Performance Notes

- Designed for low-latency, real-time messaging
- Memory-efficient for embedded IoT applications
- Adjustable client limits via configuration
- Subject to your system's thread limits and file descriptors

## Improvements

Potential areas for future enhancement:

- **TLS/SSL Encryption** - Add secure communication with certificate-based authentication
- **Authentication & Authorization** - Implement username/password validation and access controls
- **Persistent Message Storage** - Add message persistence to disk for reliability and recovery
- **Async I/O** - Migrate from threading to async I/O for improved scalability
- **Docker Support** - Add containerized deployment for easier distribution

## License

Academic project from NUST (National University of Sciences and Technology)
