TARGET: netstore-client netstore-server

CC	= g++
CFLAGS	= -std=c++17 -Wall -Wextra -O2 -DBOOST_LOG_DYN_LINK
LFLAGS	= -lboost_system -lboost_program_options -lboost_filesystem -lboost_log_setup -lboost_log -lpthread -lboost_thread

all: netstore-client netstore-server

netstore-server: server/netstore_server.cpp server/server.cpp socket/tcp_socket.cpp socket/udp_socket.cpp socket/inet_socket.cpp communication_protocol.cpp helper.cpp logger.cpp
	$(CC) $(CFLAGS) $^ $(LFLAGS) -o $@

netstore-client: client/netstore_client.cpp client/client.cpp socket/tcp_socket.cpp socket/udp_socket.cpp socket/inet_socket.cpp communication_protocol.cpp helper.cpp logger.cpp
	$(CC) $(CFLAGS) $^ $(LFLAGS) -o $@

.PHONY: clean TARGET
clean:
	rm -f netstore-server netstore-client *.o *~ *.bak