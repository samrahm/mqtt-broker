# mqtt-broker

A minimal MQTT broker implementation in C++.

## Build

```bash
make
```

## Run

```bash
make run
```

## Structure

- `src/include/client.h` - Client interface
- `src/include/server.h` - Server interface
- `src/client.cpp` - Client implementation
- `src/server.cpp` - Server implementation (broker)
- `src/main.cpp` - Entry point with example usage