#ifndef DISTRIBUTED_FILES_STORAGE_INET_SOCKET_H
#define DISTRIBUTED_FILES_STORAGE_INET_SOCKET_H


#include <cstdint>
#include <string>
#include "communication_protocol.h"

#define TTL 5
#define TCP_QUEUE_LENGTH 5

class inet_socket {
protected:
    int sock;
    in_port_t port;
    bool closed;

public:
    inet_socket();

    void set_reuse_address();

    void set_timeout(__time_t seconds, __suseconds_t microseconds);

    int get_port();

    void listen();
    /** Binds on random port **/
    void bind();

    void connect(const std::string& destination_ip, in_port_t destination_port);

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
