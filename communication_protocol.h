#ifndef DISTRIBUTED_FILES_STORAGE_COMMUNICATION_PROTOCOL_H
#define DISTRIBUTED_FILES_STORAGE_COMMUNICATION_PROTOCOL_H

#define MAX_BUFFER_SIZE 65536
#define TTL 5
#define TCP_QUEUE_LENGTH 5

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
    constexpr int max_complex_data_size = max_simple_data_size - sizeof(uint64_t); // 2^16 = IP packet has 16 bits, UDP provides datagram as a part of IP packet
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

/********************************************** MESSAGE VALIDATION ****************************************************/
bool is_expected_command(const char* command, const std::string& expected_command);

void message_validation(const ComplexMessage& message, uint64_t expected_message_seq,
        const std::string& expected_command, ssize_t message_size, const std::string& expected_data = "") ;

void message_validation(const SimpleMessage& message, uint64_t expected_message_seq,
        const std::string& expected_command, ssize_t message_size, const std::string& expected_data = "");

/*********************************************** EXCEPTION MESSAGE ****************************************************/
class invalid_message : public std::exception {
    const std::string message;
public:
    explicit invalid_message(std::string message = "Invalid message");

    const char* what() const noexcept override;
};

#endif //DISTRIBUTED_FILES_STORAGE_COMMUNICATION_PROTOCOL_H
