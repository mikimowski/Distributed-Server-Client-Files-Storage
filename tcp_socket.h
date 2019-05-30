#ifndef DISTRIBUTED_FILES_STORAGE_TCP_SOCKET_H
#define DISTRIBUTED_FILES_STORAGE_TCP_SOCKET_H

#include "inet_socket.h"

class tcp_socket : public inet_socket {
public:
    void create_socket();
    /** Enables to wrap raw socket - use it wisely! Given data should be correct, otherwise undefined behaviour. */
    void wrap_socket(int sock);

    int select(__time_t seconds, __suseconds_t microseconds = 0);
    tcp_socket accept();
    void write(char buffer[], ssize_t length);
    ssize_t read(char buffer[], ssize_t length);
};


#endif //DISTRIBUTED_FILES_STORAGE_TCP_SOCKET_H
