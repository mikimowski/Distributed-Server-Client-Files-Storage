#ifndef DISTRIBUTED_FILES_STORAGE_COMMUNICATION_PROTOCOL_H
#define DISTRIBUTED_FILES_STORAGE_COMMUNICATION_PROTOCOL_H


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

    constexpr int max_command_length = 10;
    constexpr int max_simple_data_size = 65489;
    constexpr int max_complex_data_size = max_simple_data_size - sizeof(uint64_t); // 2^16 = IP packet has 16 bits, UDP provides datagram as a part of IP packet 65489
    constexpr uint16_t simple_message_no_data_size = communication_protocol::max_command_length + sizeof(uint64_t);
    constexpr uint16_t complex_message_no_data_size = communication_protocol::max_command_length + 2 * sizeof(uint64_t);
}

/*********************************************** SIMPLE MESSAGE *******************************************************/

struct __attribute__((__packed__)) ComplexMessage {
    char command[communication_protocol::max_command_length] = "";
    uint64_t message_seq = 0;
    uint64_t param = 0;
    char data[communication_protocol::max_complex_data_size + 1] = "";

    ComplexMessage();

    ComplexMessage(uint64_t message_seq, const std::string& command, const char* data = "", uint64_t param = 0);

    void init();

    friend std::ostream& operator << (std::ostream& out, const ComplexMessage& rhs);
};

/*********************************************** COMPLEX MESSAGE ******************************************************/

struct __attribute__((__packed__)) SimpleMessage {
    char command[communication_protocol::max_command_length] = "";
    uint64_t message_seq = 0;
    char data[communication_protocol::max_simple_data_size + 1] = "";

    SimpleMessage();

    SimpleMessage(uint64_t message_seq, const std::string& command, const char* data = "");

    void init();

    friend std::ostream& operator << (std::ostream& out, const SimpleMessage& rhs);
};



#endif //DISTRIBUTED_FILES_STORAGE_COMMUNICATION_PROTOCOL_H
