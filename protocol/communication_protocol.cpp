#include <cstdint>
#include <cstring>
#include <iostream>
#include "communication_protocol.h"
#include "../utilities/helper.h"

namespace cp = communication_protocol;

static void fill_command(char command[], const std::string &str) {
    uint64_t i = 0;
    while (i < str.length()) {
        command[i] = str[i];
        i++;
    }
    while (i < cp::max_command_length)
        command[i++] = '\0';
}

ComplexMessage::ComplexMessage() {
    init();
}

ComplexMessage::ComplexMessage(uint64_t message_seq, const std::string& command, const char* data, uint64_t param) {
    this->message_seq = message_seq;
    fill_command(this->command, command);
    strncpy(this->data, data, cp::max_complex_data_size);
    this->param = param;
}

void ComplexMessage::init() {
    message_seq = 0;
    param = 0;
    memset(&command, '\0', communication_protocol::max_command_length);
    memset(&data, '\0', communication_protocol::max_complex_data_size + 1);
}

std::ostream &operator<<(std::ostream &out, const ComplexMessage &rhs) {
    char tmp[cp::max_command_length + 1];
    strncpy(tmp, rhs.command, cp::max_command_length);
    out << "[COMMAND = " << tmp << "] [MESSAGE_SEQ = " << rhs.message_seq << "] [PARAM = " << rhs.param
        << "] [DATA = " << rhs.data << "]";
    return out;
}

SimpleMessage::SimpleMessage() {
    init();
}

SimpleMessage::SimpleMessage(uint64_t message_seq, const std::string &command, const char* data) {
    this->message_seq = message_seq;
    fill_command(this->command, command);
    strncpy(this->data, data, cp::max_simple_data_size);
}

void SimpleMessage::init() {
    message_seq = 0;
    memset(&command, '\0', communication_protocol::max_command_length);
    memset(&data, '\0', communication_protocol::max_simple_data_size + 1);
}

std::ostream &operator<<(std::ostream &out, const SimpleMessage &rhs) {
    char tmp[cp::max_command_length + 1];
    strncpy(tmp, rhs.command, cp::max_command_length);
    out << "[COMMAND = " << tmp << "] [MESSAGE_SEQ = " << rhs.message_seq << "] [DATA = " << rhs.data << "]";
    return out;
}

static bool is_valid_data(const char* data, uint64_t length) {
    for (uint64_t i = 1; i < length; i++) {
        if (data[i] != '\0' && data[i - 1] == '\0')
            return false;
    }
    return true;
}

bool is_expected_command(const char* command, const std::string& expected_command) {
    uint64_t i = 0;
    for (; i < expected_command.length(); i++)
        if (command[i] != expected_command[i])
            return false;
    while (i < cp::max_command_length)
        if (command[i++] != '\0')
            return false;
    return true;
}

void message_validation(const ComplexMessage& message, uint64_t expected_message_seq,
        const std::string& expected_command, ssize_t message_size, const std::string& expected_data) {
    if (message_size < cp::complex_message_no_data_size)
        throw invalid_message("message too small");
    if (!is_expected_command(message.command, expected_command))
        throw invalid_message("invalid command");
    if (be64toh(message.message_seq) != expected_message_seq)
        throw invalid_message("invalid message seq");
    if (!is_valid_data(message.data, message_size - cp::complex_message_no_data_size))
        throw invalid_message("invalid message data");
    if (!expected_data.empty() && expected_data != message.data)
        throw invalid_message("unexpected data received");
}

void message_validation(const SimpleMessage& message, uint64_t expected_message_seq,
        const std::string& expected_command, ssize_t message_size, const std::string& expected_data) {
    if (message_size < cp::simple_message_no_data_size)
        throw invalid_message("message too small");
    if (!is_expected_command(message.command, expected_command))
        throw invalid_message("invalid command");
    if (be64toh(message.message_seq) != expected_message_seq)
        throw invalid_message("invalid message seq");
    if (!is_valid_data(message.data, message_size - cp::simple_message_no_data_size))
        throw invalid_message("invalid message data");
    if (!expected_data.empty() && expected_data != message.data)
        throw invalid_message("unexpected data received");
}

invalid_message::invalid_message(std::string message)
        : message(std::move(message)) {}

const char* invalid_message::what() const noexcept {
    return this->message.c_str();
}
