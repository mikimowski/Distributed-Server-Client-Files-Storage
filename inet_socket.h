#ifndef DISTRIBUTED_FILES_STORAGE_INET_SOCKET_H
#define DISTRIBUTED_FILES_STORAGE_INET_SOCKET_H


#include <cstdint>
#include <string>
#include "communication_protocol.h"

#define TTL 5
#define TCP_QUEUE_LENGTH 5
// TODO make abstract
class inet_socket {
protected:
    int sock;
    in_port_t port;
    bool closed;

    void fake_close();

public:

    void set_reuse_address();

    void set_timeout(__time_t seconds, __suseconds_t microseconds);

    /** @return port in big endian */
    int get_port();

    int get_sock();

    bool is_closed();

    /** Binds on random port **/
    void bind();

    /* Can be closed manually, although it's better to use RAII...*/
    void close();
    /* Closes socket */
    virtual ~inet_socket();
};


class socket_failure : public std::exception {
    const std::string message;
public:
    explicit socket_failure(std::string message);

    const char* what() const noexcept override;
};




#endif //DISTRIBUTED_FILES_STORAGE_INET_SOCKET_H
