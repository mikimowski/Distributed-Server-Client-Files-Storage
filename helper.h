#ifndef DISTRIBUTED_FILES_STORAGE_HELPER_H
#define DISTRIBUTED_FILES_STORAGE_HELPER_H

#define TTL 4
#define TCP_QUEUE_LENGTH 5

namespace communication_protocol {
    constexpr std::string_view discover_request = "HELLO";
    constexpr std::string_view discover_response = "GOOD_DAY";
    constexpr std::string_view files_list_request = "LIST";
    constexpr std::string_view files_list_response = "MY_LIST";
    constexpr std::string_view file_get_request = "GET";
    constexpr std::string_view file_get_response = "CONNECT_ME";
    constexpr std::string_view file_remove_request = "DEL";
    constexpr std::string_view file_add_request = "ADD";
    constexpr std::string_view file_add_refusal = "NO_WAY";
    constexpr std::string_view file_add_acceptance = "CAN_ADD";
}

namespace const_variables { // TODO
    constexpr int message_max_len = 10;
    constexpr int max_data_size = 65489; // 2^16 = IP packet has 16 bits, UDP provides datagram as a part of IP packet
}

struct __attribute__((__packed__)) SimpleMessage {
    char message[const_variables::message_max_len];
    uint64_t message_seq;
    char data[const_variables::max_data_size];

    SimpleMessage() {
        init();
    }

    SimpleMessage(uint64_t message_seq, const std::string_view& message, const char *data = "") {
        init();
        this->message_seq = message_seq;
        for (int i = 0; i < const_variables::message_max_len; i++)
            if (i < message.length())
                this->message[i] = message[i];
        strcpy(this->data, data);
    }


    void init() {
        message_seq = 0;
        for (char& i : message)
            i = '\0';
        for (char& i : data)
            i = '\0';
    }

    void set_message(const std::string_view &msg) {
        for (int i = 0; i < const_variables::message_max_len; i++)
            if (i < msg.length())
                this->message[i] = msg[i];
    }

    void fill_message(const std::string_view& msg, const char* data = "") {
        init();
        set_message(msg);
        strcpy(this->data, data);
    }
};

struct __attribute__((__packed__)) ComplexMessage {
    char message[const_variables::message_max_len];
    uint64_t message_seq;
    uint64_t param;
    char data[const_variables::max_data_size - sizeof(param)];

    ComplexMessage() {
        init();
    }

    ComplexMessage(uint64_t message_seq, const std::string_view& message, const char *data = "", uint64_t param = 0) {
        init();
        this->message_seq = message_seq;
        for (int i = 0; i < const_variables::message_max_len; i++)
            if (i < message.length())
                this->message[i] = message[i];
        strcpy(this->data, data);
        this->param = param;
    }

    void init() {
        message_seq = 0;
        param = 0;
        for (char& i : message)
            i = '\0';
        for (char& i : data)
            i = '\0';
    }

    void set_message(const std::string_view &msg) {
        for (int i = 0; i < const_variables::message_max_len; i++)
            if (i < msg.length())
                this->message[i] = msg[i];
    }

    void fill_message(const std::string_view& msg, const char* data = "", uint64_t param = 0) {
        init();
        set_message(msg);
        strcpy(this->data, data);
        this->param = param;
    }
};



#endif //DISTRIBUTED_FILES_STORAGE_HELPER_H
