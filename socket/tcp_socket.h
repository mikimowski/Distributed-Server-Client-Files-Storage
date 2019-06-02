#ifndef DISTRIBUTED_FILES_STORAGE_TCP_SOCKET_H
#define DISTRIBUTED_FILES_STORAGE_TCP_SOCKET_H

#include "inet_socket.h"

class tcp_socket : public inet_socket {
public:
    tcp_socket() = default;
    tcp_socket(tcp_socket &&rhs) noexcept;
    tcp_socket(tcp_socket &rhs) = delete;
    tcp_socket& operator=(tcp_socket&& rhs);

    void create_socket();
    /** Enables to wrap socket - use it wisely! Given data should be correct, otherwise undefined behaviour. */
    void wrap_socket(int sock);

    void connect(const std::string& destination_ip, in_port_t destination_port);
    void listen();
    int select(uint64_t seconds, uint64_t microseconds = 0);
    tcp_socket accept();
    void write(char buffer[], ssize_t length);
    ssize_t read(char buffer[], ssize_t length);
};


#endif //DISTRIBUTED_FILES_STORAGE_TCP_SOCKET_H
