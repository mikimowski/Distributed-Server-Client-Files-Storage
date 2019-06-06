#include <iostream>
#include <arpa/inet.h>
#include "helper.h"

bool is_valid_string(const char* str, uint64_t length) {
    uint64_t i = 0;
    while (i < length && str[i] != '\0')
        i++;

    while (i < length)
        if (str[i++] != '\0')
            return false;
    return str[length - 1] == '\0';
}

std::tuple<std::string, in_port_t> unpack_sockaddr(const struct sockaddr_in& address) {
    return {inet_ntoa(address.sin_addr), be16toh(address.sin_port)};
}

invalid_user_input::invalid_user_input(std::string message)
        : message(std::move(message)) {}

const char* invalid_user_input::what() const noexcept {
    return this->message.c_str();
}

