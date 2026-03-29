CXX = g++
# Added -pthread for threading and -g for debugging
CXXFLAGS = -std=c++17 -Wall -Wextra -Isrc -Iinclude -pthread -g -O2
TARGET = mqtt-broker
SRCDIR = src

# Make sure all your .cpp files are listed here
SOURCES = $(SRCDIR)/main.cpp $(SRCDIR)/mqttbroker.cpp $(SRCDIR)/json_parser.cpp
OBJECTS = $(SOURCES:.cpp=.o)

all: $(TARGET)

$(TARGET): $(OBJECTS)
	$(CXX) $(CXXFLAGS) -o $@ $^ -pthread

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	rm -f $(OBJECTS) $(TARGET)

run: $(TARGET)
	./$(TARGET)

.PHONY: all clean run