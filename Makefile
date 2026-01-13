CXX = g++
CXXFLAGS = -std=c++17 -Wall -Wextra -Isrc
TARGET = mqtt-broker
SRCDIR = src
SOURCES = $(SRCDIR)/main.cpp $(SRCDIR)/client.cpp $(SRCDIR)/server.cpp
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
