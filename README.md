# MQTT Broker

A lightweight MQTT broker implementation in C++ with client support.

## Overview

This project implements an MQTT broker that supports core MQTT operations including client connections, message publishing, and topic subscriptions. The broker loads its configuration from a JSON file and handles multiple concurrent clients.

## Features

- **Client-Broker Architecture**: Includes both a broker server and a client library
- **MQTT Protocol Support**: Implements key MQTT signals (CONNECT, PUBLISH, SUBSCRIBE, DISCONNECT)
- **Topic Subscriptions**: Supports topic-based message routing with wildcard matching
- **Session Management**: Maintains client sessions with clean session flags
- **JSON Configuration**: Configurable broker settings via JSON config file
- **Logging**: Built-in logging and monitoring capabilities

## Project Structure

- `include/` - Header files
  - `mqttbroker.h` - Broker implementation
  - `client.h` - MQTT client interface
  - `json_parser.h` - JSON configuration parser
  - `logger.h` - Logging utilities
  - `mqtt_utils.h` - MQTT utility functions
- `src/` - Source files
  - `main.cpp` - Entry point with broker initialization
  - `mqttbroker.cpp` - Broker implementation
  - `json_parser.cpp` - JSON configuration loading
- `client/` - Client library
  - `client.cpp` - Client implementation
- `config/` - Configuration
  - `config.json` - Broker configuration (port, max clients, log level)
- `tests/` - Test suite

## Configuration

Edit `config/config.json` to configure the broker:

```json
{
  "port": 1884,
  "max_clients": 10,
  "log_level": "info"
}
```

- **port**: The port on which the broker listens
- **max_clients**: Maximum number of concurrent clients
- **log_level**: Logging level (info, debug, etc.)

## Build

```bash
make
```

## Run

```bash
make run
```

## Usage

The broker starts and listens for incoming MQTT client connections on the configured port. Clients can:

- Connect to the broker with a client ID
- Publish messages to topics
- Subscribe to topics to receive published messages
- Disconnect gracefully
