# Build stage
FROM gcc:11 as builder

WORKDIR /app

# Install build dependencies including nlohmann-json
RUN apt-get update && apt-get install -y --no-install-recommends \
    nlohmann-json3-dev \
    make \
    && rm -rf /var/lib/apt/lists/*

# Copy project files
COPY include/ ./include/
COPY src/ ./src/
COPY config/ ./config/
COPY Makefile .

# Build the broker
RUN make clean && make

# Runtime stage
FROM debian:bookworm-slim

WORKDIR /app

# Install only runtime dependencies
RUN apt-get update && apt-get install -y --no-install-recommends \
    libstdc++6 \
    && rm -rf /var/lib/apt/lists/*

# Copy built binary and config from builder
COPY --from=builder /app/mqtt-broker .
COPY --from=builder /app/config/config.json ./config/

# Expose MQTT port
EXPOSE 1884

# Run the broker
CMD ["./mqtt-broker"]
