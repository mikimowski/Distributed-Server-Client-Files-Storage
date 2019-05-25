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
    constexpr int random_seed = 0;
}

namespace communication_protocol {
    const char* discover_request = "HELLO";
    const char* discover_response = "GOOD_DAY";
    const char* files_list_request = "LIST";
    const char* files_list_response = "MY_LIST";
    const char* file_get_request = "GET";
    const char* file_get_response = "CONNECT_ME";
    const char* file_remove_request = "DEL";
    const char* file_add_request = "ADD";
    const char* file_add_refusal = "NO_WAY";
    const char* file_add_acceptance = "CAN_ADD";
}


struct __attribute__((__packed__)) ComplexMessage {
    char command[const_variables::max_command_length];
    uint64_t message_seq;
    uint64_t param;
    char data[const_variables::max_complex_data_size];

    ComplexMessage() {
        init();
    }

    ComplexMessage(uint64_t message_seq, const std::string_view& command, const char *data = "", uint64_t param = 0) {
        init();
        this->message_seq = message_seq;
        for (int i = 0; i < const_variables::max_command_length; i++)
            if (i < command.length())
                this->command[i] = command[i];
        strcpy(this->data, data);
        this->param = param;
    }

    void init() {
        message_seq = 0;
        param = 0;
        for (char& i : command)
            i = '\0';
        for (char& i : data)
            i = '\0';
    }

    void set_message(const std::string_view &msg) {
        for (int i = 0; i < const_variables::max_command_length; i++)
            if (i < msg.length())
                this->command[i] = msg[i];
    }

    void fill_message(const std::string_view& msg, const char* data = "", uint64_t param = 0) {
        init();
        set_message(msg);
        strcpy(this->data, data);
        this->param = param;
    }

    friend std::ostream& operator << (std::ostream &out, ComplexMessage& rhs) {
        out << "[COMMAND = "<< rhs.command<< "] [MESSAGE_SEQ = " << rhs.message_seq <<"] [PARAM = " << rhs.param << "] [DATA = " << rhs.data << "]";
        return out;
    }
};


struct __attribute__((__packed__)) SimpleMessage {
    char command[const_variables::max_command_length];
    uint64_t message_seq;
    char data[const_variables::max_simple_data_size];

    SimpleMessage() {
        init();
    }

    SimpleMessage(uint64_t message_seq, const std::string_view& command, const char *data = "") {
        init();
        this->message_seq = message_seq;
        for (int i = 0; i < const_variables::max_command_length; i++)
            if (i < command.length())
                this->command[i] = command[i];
        strcpy(this->data, data);
    }

//    SimpleMessage(const ComplexMessage& message) {
//        strcpy(this->command, message.command);
//        this->message_seq = message.message_seq;
//        strcpy(this->data, message.data);
//    }

    void init() {
        message_seq = 0;
        for (char& i : command)
            i = '\0';
        for (char& i : data)
            i = '\0';
    }

    void set_message(const std::string_view &msg) {
        for (int i = 0; i < const_variables::max_command_length; i++)
            if (i < msg.length())
                this->command[i] = msg[i];
    }

    void fill_message(const std::string_view& msg, const char* data = "") {
        init();
        set_message(msg);
        strcpy(this->data, data);
    }

    friend std::ostream& operator << (std::ostream &out, SimpleMessage& rhs) {
        out << "[COMMAND = "<< rhs.command<< "] [MESSAGE_SEQ = " << rhs.message_seq <<"] [DATA = " << rhs.data << "]";
        return out;
    }
};

/// Must be ended with '\0'
bool is_correct_string(const char* str, uint64_t max_len) {
    uint64_t i = 0;
    while (i < max_len && str[i] != '\0')
        i++;

    while (i < max_len)
        if (str[i++] != '\0')
            return false;
    return str[max_len - 1] == '\0';
}

bool is_correct_files_list(const char* str, uint64_t max_len) {
    uint64_t i = 0;
    while (i < max_len && str[i] != '\0')
        i++;

    while (i < max_len)
        if (str[i++] != '\0')
            return false;
    return str[max_len - 1] == '\0' || str[max_len - 1] == '\n';
}

/**
 * @return True if given pattern is a substring of given string,
 *         false otherwise
 */
bool is_substring(char const* pattern, const std::string_view& str) {
    return str.find(pattern) != std::string::npos;
}

void display_log_separator() {
    std::cout << "[------------------------------------------------------------------------]" << std::endl;
}

size_t get_file_size(const std::string& file);

#endif //DISTRIBUTED_FILES_STORAGE_HELPER_H
