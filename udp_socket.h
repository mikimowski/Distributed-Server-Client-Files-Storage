#ifndef DISTRIBUTED_FILES_STORAGE_UDP_SOCKET_H
#define DISTRIBUTED_FILES_STORAGE_UDP_SOCKET_H

#include "inet_socket.h"
#include <tuple>

class udp_socket : public inet_socket {

public:
    udp_socket();
    udp_socket(udp_socket& rhs) = delete;

    void create_socket();
    void create_multicast_socket();

    void join_multicast_group(const std::string& multicast_address);
    void leave_multicast_group(const std::string& multicast_address);

    /** Binds on given port **/
    void bind(in_port_t port);

    void send(const SimpleMessage& message, const std::string& destination_ip, in_port_t destination_port, uint16_t data_length);
    void send(const ComplexMessage& message, const std::string& destination_ip, in_port_t destination_port, uint16_t data_length);
    void send(const SimpleMessage& message, const struct sockaddr_in& destination_address, uint16_t data_length = 0);
    void send(const ComplexMessage& message, const struct sockaddr_in& destination_address, uint16_t data_length = 0);

    /** Reads received data into ComplexMessage and returns it */
    std::tuple<ComplexMessage, ssize_t, struct sockaddr_in> recvfrom_complex();
    std::tuple<SimpleMessage, ssize_t, struct sockaddr_in> recvfrom_simple();
};


#endif //DISTRIBUTED_FILES_STORAGE_UDP_SOCKET_H
