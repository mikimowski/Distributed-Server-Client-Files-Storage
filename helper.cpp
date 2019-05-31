#include <boost/filesystem.hpp>
#include <sys/socket.h>
#include <chrono>
#include <iostream>

#include "helper.h"
#include "err.h"

namespace fs = boost::filesystem;

using std::string;


#include <utility>
#include <thread>


/// Must be ended with '\0'
bool is_valid_string(const char* str, uint64_t max_len) {
    uint64_t i = 0;
    while (i < max_len && str[i] != '\0')
        i++;

    while (i < max_len)
        if (str[i++] != '\0')
            return false;
    return str[max_len - 1] == '\0';
}

bool is_valid_data(const char* data, uint64_t length) {
    for (uint64_t i = 1; i < length; i++) {
        if (data[i] != '\0' && data[i - 1] == '\0')
            return false;
    }
    return true;
}

/**
 * @return True if given pattern is a substring of given string,
 *         false otherwise
 */
bool is_substring(const std::string &pattern, const std::string &str) {
    return str.find(pattern) != std::string::npos;
}

void display_log_separator() {
    std::cout << "[------------------------------------------------------------------------]" << std::endl;
}

invalid_message::invalid_message(std::string message)
        : message(std::move(message)) {}

const char* invalid_message::what() const noexcept {
    return this->message.c_str();
}


invalid_user_input::invalid_user_input(std::string message)
        : message(std::move(message)) {}

const char* invalid_user_input::what() const noexcept {
    return this->message.c_str();
}

