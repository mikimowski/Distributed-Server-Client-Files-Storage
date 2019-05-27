TARGET: netstore-client netstore-server

CC	= g++
CFLAGS	= -std=c++17 -DBOOST_LOG_DYN_LINK
LFLAGS	= -lboost_system -lboost_program_options -lboost_filesystem -lboost_log_setup -lboost_log -lboost_thread -lpthread

all: netstore-client netstore-server

netstore-server: server.cpp helper.cpp err.cpp
	$(CC) $(CFLAGS) $^ $(LFLAGS) -o $@

netstore-client: client.cpp helper.cpp err.cpp
	$(CC) $(CFLAGS) $^ $(LFLAGS) -o $@

.PHONY: clean TARGET
clean:
	rm -f netstore-server netstore-client *.o *~ *.bak