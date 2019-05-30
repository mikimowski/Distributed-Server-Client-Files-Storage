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

#define MAX_BUFFER_SIZE 50


/**
 * Checks whether given string is filled with '\0' from starting index included to the end index excluded
 * str[start; end)
 * @param str
 * @param start
 * @param end
 * @return
 */
bool is_empty(const char *str, size_t start, size_t end) {
    for (size_t i = start; i < end; i++)
        if (str[i] != '\0')
            return false;
    return true;
}

/// Must be ended with '\0'
bool is_valid_string(const char *str, uint64_t max_len) {
    uint64_t i = 0;
    while (i < max_len && str[i] != '\0')
        i++;

    while (i < max_len)
        if (str[i++] != '\0')
            return false;
    return str[max_len - 1] == '\0';
}

bool is_valid_data(const char *data, uint64_t length) {
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

void set_socket_timeout(int socket, uint16_t microseconds) {
    struct timeval timeval{};
    timeval.tv_usec = microseconds;

    if (setsockopt(socket, SOL_SOCKET, SO_RCVTIMEO, (void *) &timeval, sizeof(timeval)) < 0)
        syserr("setsockopt 'SO_RCVTIMEO'");
}


// TODO wrong file handle...
size_t get_file_size(const string &file) {
    fs::path file_path{file};
    return fs::file_size(file_path);
}


invalid_message::invalid_message(std::string message)
        : message(std::move(message)) {}

const char *invalid_message::what() const noexcept {
    return this->message.c_str();
}


invalid_user_input::invalid_user_input(std::string message)
        : message(std::move(message)) {}

const char *invalid_user_input::what() const noexcept {
    return this->message.c_str();
}

