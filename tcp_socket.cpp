#include <unistd.h>
#include <stdint.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <cstring>

#include "tcp_socket.h"


void tcp_socket::create_socket() {
    if ((sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)) < 0)
        throw socket_failure("socket");
    closed = false;
}

void tcp_socket::wrap_socket(int sock) {
    this->sock = sock;
    closed = false;
}

int tcp_socket::select(__time_t seconds, __suseconds_t microseconds) {
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
    int sock;
    struct sockaddr_in source_address{};
    memset(&source_address, 0, sizeof(source_address));
    socklen_t len = sizeof(source_address);

    if ((sock = ::accept(port, (struct sockaddr *) &source_address, &len)) < 0)
        throw socket_failure("accept");
    tcp_socket new_sock;
    new_sock.wrap_socket(sock);

    return new_sock;
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