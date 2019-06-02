#include <utility>

#include <mutex>
#include <unistd.h>
#include <stdint.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <cstring>
#include <netdb.h>

#include "inet_socket.h"
#include "../logger.h"

namespace cp = communication_protocol;


inet_socket::~inet_socket() {
    if (!closed) {
        if (::close(sock) < 0)
            logger::syserr("close");
    }
}

void inet_socket::fake_close() {
    closed = true;
}

/****************************************************** SETUP *********************************************************/

void inet_socket::bind() {
    struct sockaddr_in local_addr {};
    socklen_t addrlen = sizeof(local_addr);
    memset(&local_addr, 0, sizeof(local_addr)); // sin_port set to 0, therefore bind will be set random free port
    local_addr.sin_family = AF_INET;
    local_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    if (::bind(sock, (struct sockaddr*) &local_addr, sizeof(local_addr)) < 0)
        throw socket_failure("bind");

    memset(&local_addr, 0, sizeof(local_addr));
    if (::getsockname(sock, (struct sockaddr*) &local_addr, &addrlen) < 0)
        throw socket_failure("getsockname");

    port = local_addr.sin_port;
}

void inet_socket::set_timeout(struct timeval timeval) {
    if (::setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (void *) &timeval, sizeof(timeval)) < 0)
        throw socket_failure("setsockopt 'SO_RCVTIMEO'");
}

void inet_socket::set_reuse_address() {
    int reuse = 1;
    if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) < 0)
        throw socket_failure("setsockopt 'SO_REUSEADDR'");
}

int inet_socket::get_port() {
    return port;
}

int inet_socket::get_sock() {
    return sock;
}

bool inet_socket::is_closed() {
    return closed;
}

socket_failure::socket_failure(std::string message)
    : message(std::move(message))
    {}

const char* socket_failure::what() const noexcept {
    return this->message.c_str();
}
