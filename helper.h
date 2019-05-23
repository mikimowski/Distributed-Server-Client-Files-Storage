#ifndef DISTRIBUTED_FILES_STORAGE_HELPER_H
#define DISTRIBUTED_FILES_STORAGE_HELPER_H

#define TTL 4
#define TCP_QUEUE_LENGTH 5



namespace const_variables { // TODO
    constexpr int max_command_len = 10;
    constexpr int max_data_size = 3000; // 2^16 = IP packet has 16 bits, UDP provides datagram as a part of IP packet 65489
    constexpr uint16_t simple_command_min_length = const_variables::max_command_len + sizeof(uint64_t);
    constexpr uint16_t complex_command_min_length = const_variables::max_command_len + 2 * sizeof(uint64_t);
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


//    constexpr uint16_t discover_response_message_size = ;
}


struct __attribute__((__packed__)) ComplexMessage {
    char command[const_variables::max_command_len];
    uint64_t message_seq;
    uint64_t param;
    char data[const_variables::max_data_size - sizeof(param)];

    ComplexMessage() {
        init();
    }

    ComplexMessage(uint64_t message_seq, const std::string_view& message, const char *data = "", uint64_t param = 0) {
        init();
        this->message_seq = message_seq;
        for (int i = 0; i < const_variables::max_command_len; i++)
            if (i < message.length())
                this->command[i] = message[i];
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
        for (int i = 0; i < const_variables::max_command_len; i++)
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
        out << "MESSAGE = " << rhs.command << std::endl;
        out << "MESSAGE_SEQ = " << rhs.message_seq << std::endl;
        out << "PARAM = " << rhs.param << std::endl;
        out << "DATA = " << rhs.data << std::endl;
        return out;
    }
};


struct __attribute__((__packed__)) SimpleMessage {
    char command[const_variables::max_command_len];
    uint64_t message_seq;
    char data[const_variables::max_data_size];

    SimpleMessage() {
        init();
    }

    SimpleMessage(uint64_t message_seq, const std::string_view& message, const char *data = "") {
        init();
        this->message_seq = message_seq;
        for (int i = 0; i < const_variables::max_command_len; i++)
            if (i < message.length())
                this->command[i] = message[i];
        strcpy(this->data, data);
    }

    SimpleMessage(const ComplexMessage& message) {
        strcpy(this->command, message.command);
        this->message_seq = message.message_seq;
        strcpy(this->data, message.data);
    }

    void init() {
        message_seq = 0;
        for (char& i : command)
            i = '\0';
        for (char& i : data)
            i = '\0';
    }

    void set_message(const std::string_view &msg) {
        for (int i = 0; i < const_variables::max_command_len; i++)
            if (i < msg.length())
                this->command[i] = msg[i];
    }

    void fill_message(const std::string_view& msg, const char* data = "") {
        init();
        set_message(msg);
        strcpy(this->data, data);
    }

    friend std::ostream& operator << (std::ostream &out, SimpleMessage& rhs) {
        out << "MESSAGE = " << rhs.command << std::endl;
        out << "MESSAGE_SEQ = " << rhs.message_seq << std::endl;
        out << "DATA = " << rhs.data << std::endl;
        return out;
    }
};



#endif //DISTRIBUTED_FILES_STORAGE_HELPER_H
