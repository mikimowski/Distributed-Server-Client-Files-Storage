#ifndef DISTRIBUTED_FILES_STORAGE_HELPER_H
#define DISTRIBUTED_FILES_STORAGE_HELPER_H

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
bool is_empty(const char* str, size_t start, size_t end);

/// Must be ended with '\0'
bool is_valid_string(const char* str, uint64_t max_len);

bool is_valid_data(const char* data, uint64_t length);


template<typename... A>
void handler(A &&... args) {
    std::thread handler{std::forward<A>(args)...};
    handler.detach();
}


/**
 * @return True if given pattern is a substring of given string,
 *         false otherwise
 */
bool is_substring(const std::string &pattern, const std::string &str);

void display_log_separator();

size_t get_file_size(const std::string &file);

void set_socket_timeout(int socket, uint16_t microseconds = 1000);

class invalid_message : public std::exception {
    const std::string message;
public:
    explicit invalid_message(std::string message = "Invalid message");

    const char* what() const noexcept override;
};

class invalid_user_input : public std::exception {
    const std::string message;
public:
    explicit invalid_user_input(std::string message = "Invalid message");

    const char* what() const noexcept override;
};

#endif //DISTRIBUTED_FILES_STORAGE_HELPER_H
