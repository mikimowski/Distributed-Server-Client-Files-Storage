#include <utility>
#include <thread>

#ifndef DISTRIBUTED_FILES_STORAGE_HELPER_H
#define DISTRIBUTED_FILES_STORAGE_HELPER_H

#define TTL 4
#define TCP_QUEUE_LENGTH 5
#define MAX_BUFFER_SIZE 50


namespace const_variables { // TODO
    constexpr int max_command_length = 10;
    constexpr int max_simple_data_size = 65489;
    constexpr int max_complex_data_size = max_simple_data_size - sizeof(uint64_t); // 2^16 = IP packet has 16 bits, UDP provides datagram as a part of IP packet 65489
    constexpr uint16_t simple_message_no_data_size = const_variables::max_command_length + sizeof(uint64_t);
    constexpr uint16_t complex_message_no_data_size = const_variables::max_command_length + 2 * sizeof(uint64_t);
}

namespace communication_protocol {
    const std::string discover_request = "HELLO";
    const std::string discover_response = "GOOD_DAY";
    const std::string files_list_request = "LIST";
    const std::string files_list_response = "MY_LIST";
    const std::string file_get_request = "GET";
    const std::string file_get_response = "CONNECT_ME";
    const std::string file_remove_request = "DEL";
    const std::string file_add_request = "ADD";
    const std::string file_add_refusal = "NO_WAY";
    const std::string file_add_acceptance = "CAN_ADD";
}


struct __attribute__((__packed__)) ComplexMessage {
    char command[const_variables::max_command_length];
    uint64_t message_seq;
    uint64_t param;
    char data[const_variables::max_complex_data_size + 1];

    ComplexMessage() {
        init();
    }

    ComplexMessage(uint64_t message_seq, const std::string& command, const char *data = "", uint64_t param = 0) {
        init();
        this->message_seq = message_seq;
        strcpy(this->command, command.c_str());
        strcpy(this->data, data);
        this->param = param;
    }

    void init() {
        message_seq = 0;
        param = 0;
        memset(&command, '\0', const_variables::max_command_length);
        memset(&data, '\0', const_variables::max_complex_data_size + 1);
    }

    friend std::ostream& operator << (std::ostream& out, ComplexMessage& rhs) {
        out << "[COMMAND = "<< rhs.command << "] [MESSAGE_SEQ = " << rhs.message_seq <<"] [PARAM = " << rhs.param << "] [DATA = " << rhs.data << "]";
        return out;
    }
};


struct __attribute__((__packed__)) SimpleMessage {
    char command[const_variables::max_command_length];
    uint64_t message_seq;
    char data[const_variables::max_simple_data_size + 1];

    SimpleMessage() {
        init();
    }

    SimpleMessage(uint64_t message_seq, const std::string& command, const char* data = "") {
        init();
        this->message_seq = message_seq;
        strcpy(this->command, command.c_str());
        strcpy(this->data, data);
    }

    void init() {
        message_seq = 0;
        memset(&command, '\0', const_variables::max_command_length);
        memset(&data, '\0', const_variables::max_simple_data_size + 1);
    }

    friend std::ostream& operator << (std::ostream& out, SimpleMessage& rhs) {
        out << "[COMMAND = "<< rhs.command << "] [MESSAGE_SEQ = " << rhs.message_seq <<"] [DATA = " << rhs.data << "]";
        return out;
    }
};



/**
 * Checks whether given string is filled with '\0' from starting index included to the end index excluded
 * str[start; end)
 * @param str
 * @param start
 * @param end
 * @return
 */
bool is_empty(const char* str, size_t start, size_t end) {
    for (size_t  i = start; i < end; i++)
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

template<typename... A>
void handler(A&&... args) {
    std::thread handler {std::forward<A>(args)...};
    handler.detach();
}


/**
 * @return True if given pattern is a substring of given string,
 *         false otherwise
 */
bool is_substring(const std::string& pattern, const std::string& str) {
    return str.find(pattern) != std::string::npos;
}

void display_log_separator() {
    std::cout << "[------------------------------------------------------------------------]" << std::endl;
}

size_t get_file_size(const std::string& file);

void set_socket_timeout(int socket, uint16_t microseconds = 1000);

class invalid_command : public std::exception {
    const std::string message;
public:
    invalid_command(std::string message = "Invalid command")
    : message(std::move(message))
    {}

    virtual const char* what() const throw() {
        return this->message.c_str();
    }
};

class invalid_message : public std::exception {
    const std::string message;
public:
    invalid_message(std::string message = "Invalid message")
    : message(std::move(message))
    {}

    virtual const char* what() const throw() {
        return this->message.c_str();
    }
};

class invalid_user_input : public std::exception {
    const std::string message;
public:
    invalid_user_input(std::string message = "Invalid message")
    : message(std::move(message))
    {}

    virtual const char* what() const throw() {
        return this->message.c_str();
    }
};

#endif //DISTRIBUTED_FILES_STORAGE_HELPER_H
