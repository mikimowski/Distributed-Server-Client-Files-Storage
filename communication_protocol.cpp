#include <cstdint>
#include <cstring>
#include <iostream>
#include "communication_protocol.h"

namespace cp = communication_protocol;

ComplexMessage::ComplexMessage() {
    init();
}

ComplexMessage::ComplexMessage(uint64_t message_seq, const std::string& command, const char* data, uint64_t param) {
    this->message_seq = message_seq;
    strncpy(this->command, command.c_str(), cp::max_command_length);
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
//    init();
    this->message_seq = message_seq;
    strncpy(this->command, command.c_str(), cp::max_command_length);
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