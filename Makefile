TARGET: netstore-server netstore-client



netstore-server:
	g++ -std=c++17 -L/usr/bin -lboost_program_options -lboost_filesystem -lboost_system -o netstore-server netstore-server.cpp

netstore-client:
	g++ -std=c++17 -L/usr/bin -lboost_program_options -lboost_filesystem -lboost_system -o netstore-client netstore-client.cpp

.PHONY: clean TARGET
clean:
	rm -f netstore-server netstore-client *.o *~ *.bak