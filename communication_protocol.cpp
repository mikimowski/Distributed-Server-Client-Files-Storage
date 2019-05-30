#include <cstdint>
#include <cstring>
#include <iostream>
#include "communication_protocol.h"


ComplexMessage::ComplexMessage() {
    init();
}

ComplexMessage::ComplexMessage(uint64_t message_seq, const std::string &command, const char* data, uint64_t param) {
    init();
    this->message_seq = message_seq;
    strcpy(this->command, command.c_str());
    strcpy(this->data, data);
    this->param = param;
}

void ComplexMessage::init() {
    message_seq = 0;
    param = 0;
    memset(&command, '\0', communication_protocol::max_command_length);
    memset(&data, '\0', communication_protocol::max_complex_data_size + 1);
}

std::ostream &operator<<(std::ostream &out, const ComplexMessage &rhs) {
    out << "[COMMAND = " << rhs.command << "] [MESSAGE_SEQ = " << rhs.message_seq << "] [PARAM = " << rhs.param
        << "] [DATA = " << rhs.data << "]";
    return out;
}

SimpleMessage::SimpleMessage() {
    init();
}

SimpleMessage::SimpleMessage(uint64_t message_seq, const std::string &command, const char* data) {
    init();
    this->message_seq = message_seq;
    strcpy(this->command, command.c_str());
    strcpy(this->data, data);
}

void SimpleMessage::init() {
    message_seq = 0;
    memset(&command, '\0', communication_protocol::max_command_length);
    memset(&data, '\0', communication_protocol::max_simple_data_size + 1);
}

std::ostream &operator<<(std::ostream &out, const SimpleMessage &rhs) {
    out << "[COMMAND = " << rhs.command << "] [MESSAGE_SEQ = " << rhs.message_seq << "] [DATA = " << rhs.data << "]";
    return out;
}