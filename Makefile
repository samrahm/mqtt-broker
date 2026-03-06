CXX = g++
CXXFLAGS = -std=c++17 -Wall -Wextra -Isrc -Iinclude
TARGET = mqtt-broker
SRCDIR = src
SOURCES = $(SRCDIR)/main.cpp $(SRCDIR)/mqttbroker.cpp $(SRCDIR)/json_parser.cpp
OBJECTS = $(SOURCES:.cpp=.o)

all: $(TARGET)

$(TARGET): $(OBJECTS)
	$(CXX) $(CXXFLAGS) -o $@ $^

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	rm -f $(OBJECTS) $(TARGET)

run: $(TARGET)
	./$(TARGET)

.PHONY: all clean run
