CXX = g++
CXXFLAGS = -Wall -Wextra -std=c++17 -pthread -g
TARGETS = multi_server chat_client

all: $(TARGETS)

multi_server: multi_server.cc chat_protocol.h
	$(CXX) $(CXXFLAGS) -o $@ multi_server.cc

chat_client: chat_client.cc chat_protocol.h ws_handshake.h
	$(CXX) $(CXXFLAGS) -o $@ chat_client.cc

tsan: CXXFLAGS += -fsanitize=thread -O1
tsan: clean all

clean:
	rm -f multi_server chat_client

.PHONY: all clean tsan
