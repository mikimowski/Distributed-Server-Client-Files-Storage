#include <unistd.h>
#include <stdint.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <cstring>

#include "tcp_socket.h"

tcp_socket::tcp_socket(tcp_socket &&rhs) noexcept {
    sock = rhs.get_sock();
    port = rhs.get_port();
    closed = rhs.is_closed();
    rhs.fake_close();
}


tcp_socket& tcp_socket::operator=(tcp_socket&& rhs) {
    if (this != &rhs) {
        sock = rhs.get_sock();
        port = rhs.get_port();
        closed = rhs.is_closed();
        rhs.fake_close();
    }

    return *this;
}

void tcp_socket::create_socket() {
    if ((sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)) < 0)
        throw socket_failure("socket");
    struct sockaddr_in local_addr {};
    socklen_t addrlen = sizeof(local_addr);
    memset(&local_addr, 0, sizeof(local_addr));
    if (::getsockname(sock, (struct sockaddr*) &local_addr, &addrlen) < 0)
        throw socket_failure("getsockname");
    port = local_addr.sin_port;
    closed = false;
}

void tcp_socket::wrap_socket(int sock) {
    struct sockaddr_in local_addr {};
    socklen_t addrlen = sizeof(local_addr);
    memset(&local_addr, 0, sizeof(local_addr));
    if (::getsockname(sock, (struct sockaddr*) &local_addr, &addrlen) < 0)
        throw socket_failure("getsockname");
    port = local_addr.sin_port;
    this->sock = sock;
    closed = false;
}

void tcp_socket::connect(const std::string& destination_ip, in_port_t destination_port) {
    struct sockaddr_in destination_address{};
    memset(&destination_address, 0, sizeof(destination_address));
    destination_address.sin_family = AF_INET;
    destination_address.sin_port = destination_port;
    if (::inet_aton(destination_ip.c_str(), &destination_address.sin_addr) == 0)
        throw socket_failure("inet_aton");
    if (::connect(sock, (struct sockaddr*) &destination_address, sizeof(destination_address)) < 0)
        throw socket_failure("connect");
}

void tcp_socket::listen() {
    if (::listen(sock, TCP_QUEUE_LENGTH) < 0)
        throw socket_failure("listen");
}

int tcp_socket::select(uint64_t seconds, uint64_t microseconds) {
    fd_set set;
    struct timeval timeval{};
    timeval.tv_sec = seconds;
    timeval.tv_usec = microseconds;

    FD_ZERO(&set);
    FD_SET(sock, &set);

    int rv = ::select(sock + 1, &set, nullptr, nullptr, &timeval);
    if (rv == -1)
        throw socket_failure("select");
    return rv;
}


tcp_socket tcp_socket::accept() {
    int fd;
    struct sockaddr_in source_address{};
    memset(&source_address, 0, sizeof(source_address));
    socklen_t len = sizeof(source_address);

    if ((fd = ::accept(sock, (struct sockaddr *) &source_address, &len)) < 0)
        throw socket_failure("accept");
    tcp_socket new_socket;
    new_socket.wrap_socket(fd);

    return new_socket;
}

void tcp_socket::write(char buffer[], ssize_t length) {
    if (::write(sock, buffer, length) != length)
        throw socket_failure("partial write");
}

ssize_t tcp_socket::read(char buffer[], ssize_t length) {
    ssize_t read_len;
    if ((read_len = ::read(sock, buffer, length)) < 0)
        throw socket_failure("read");
    return read_len;
}