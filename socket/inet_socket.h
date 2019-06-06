#ifndef DISTRIBUTED_FILES_STORAGE_INET_SOCKET_H
#define DISTRIBUTED_FILES_STORAGE_INET_SOCKET_H

#include <cstdint>
#include <string>
#include "../protocol/communication_protocol.h"

class inet_socket {
protected:
    int sock;
    in_port_t port;
    bool closed;

    void fake_close();

public:
    /* Closes socket */
    virtual ~inet_socket();

    void set_reuse_address();

    /** Sets socket timeout according to given parameter */
    void set_timeout(struct timeval timeval);

    /** @return port in big endian */
    int get_port();

    int get_sock();

    bool is_closed();

    /** Binds on random port **/
    void bind();
};


class socket_failure : public std::exception {
    const std::string message;
public:
    explicit socket_failure(std::string message);

    const char* what() const noexcept override;
};




#endif //DISTRIBUTED_FILES_STORAGE_INET_SOCKET_H
