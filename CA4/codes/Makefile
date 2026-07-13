CXX := g++
CXXFLAGS := -std=c++17 -O2 -Wall -Wextra

HEADERS := packet.hpp timer.hpp logger.hpp

.PHONY: all clean

all: sender receiver

sender: sender.cpp $(HEADERS)
	$(CXX) $(CXXFLAGS) sender.cpp -o sender

receiver: receiver.cpp $(HEADERS)
	$(CXX) $(CXXFLAGS) receiver.cpp -o receiver

clean:
	rm -f sender receiver
