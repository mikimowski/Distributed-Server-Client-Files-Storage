#include <mutex>
#include <unistd.h>
#include <stdint.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>

#include "inet_socket.h"
#include "err.h"

namespace cp = communication_protocol;

inet_socket::inet_socket()
    : closed(true),
    port(-1),
    sock(-1)
    {}

inet_socket::~inet_socket() {
    if (!closed) {
        if (::close(sock) < 0)
            msgerr("socket closing failure");
    }
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
    if (::getsockname(sock, (struct sockaddr*) &local_addr, &addrlen) < 0)
        throw socket_failure("getsockname");

    port = be16toh(local_addr.sin_port);
}

void inet_socket::listen() {
    if (::listen(sock, TCP_QUEUE_LENGTH) < 0)
        throw socket_failure("listen");
}

void inet_socket::connect(const std::string& destination_ip, in_port_t destination_port) {
    struct sockaddr_in destination_address{};
    memset(&destination_address, 0, sizeof(destination_address));
    destination_address.sin_family = AF_INET;
    destination_address.sin_port = htobe16(destination_port);
    if (::inet_aton(destination_ip.c_str(), &destination_address.sin_addr) == 0)
        throw socket_failure("inet_aton");
    if (::connect(sock, (struct sockaddr*) &destination_address, sizeof(destination_address)) < 0)
        throw socket_failure("connect");
}

void inet_socket::set_timeout(__time_t seconds, __suseconds_t microseconds) {
    struct timeval timeval{};
    timeval.tv_sec = seconds;
    timeval.tv_usec = microseconds;

    if (::setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (void *) &timeval, sizeof(timeval)) < 0)
        throw socket_failure("setsockopt 'SO_RCVTIMEO'");
}



///**
// *
// * @return True if socket was successfully closed, false if it was already closed.
// */
//bool inet_socket::close()  {
//    static std::mutex mutex;
//    std::lock_guard<std::mutex> lock(mutex);
//    if (closed)
//        return false;
//    if (close(socket) < 0)
//        throw socket_failure("socket close");
//    closed = true;
//    return true;
//}
//
