#ifndef DISTRIBUTED_FILES_STORAGE_TCP_SOCKET_H
#define DISTRIBUTED_FILES_STORAGE_TCP_SOCKET_H

#include "inet_socket.h"

class tcp_socket : inet_socket {
public:
    void create_socket();
};


#endif //DISTRIBUTED_FILES_STORAGE_TCP_SOCKET_H
