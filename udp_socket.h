#ifndef DISTRIBUTED_FILES_STORAGE_UDP_SOCKET_H
#define DISTRIBUTED_FILES_STORAGE_UDP_SOCKET_H

#include "inet_socket.h"

class udp_socket : inet_socket {

public:
    udp_socket();

    void create_socket();
    void create_multicast_socket();

    void join_multicast_group(const std::string& multicast_address);
    void leave_multicast_group(const std::string& multicast_address);

    void send(const std::string& destination_ip, in_port_t destination_port, const SimpleMessage& message, uint16_t data_length);
    void send(const SimpleMessage& message, const struct sockaddr_in& destination_address, uint16_t data_length);
    void send(const ComplexMessage& message, const struct sockaddr_in& destination_address, uint16_t data_length);
};


#endif //DISTRIBUTED_FILES_STORAGE_UDP_SOCKET_H
