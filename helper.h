#ifndef DISTRIBUTED_FILES_STORAGE_HELPER_H
#define DISTRIBUTED_FILES_STORAGE_HELPER_H

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

namespace { // TODO
    constexpr int cmd_max_len = 10;
}

struct __attribute__((__packed__)) simple_cmd {
    char cmd[cmd_max_len];
    uint64_t cmd_seq;
    char data[];
};

struct __attribute__((__packed__)) complex_cmd {
    char cmd[cmd_max_len];
    uint64_t cmd_seq;
    uint64_t param;
    char data[];
};


// TODO w końcu 0 czy \0?
void set_cmd(struct simple_cmd& cmd_struct, const std::string_view& cmd) {
    for (int i = 0; i < cmd_max_len; i++)
        if (i < cmd.length())
            cmd_struct.cmd[i] = cmd[i];
        else
            cmd_struct.cmd[i] = '\0';
}

void set_cmd(struct complex_cmd& cmd_struct, const std::string_view& cmd) {
    for (int i = 0; i < cmd_max_len; i++)
        if (i < cmd.length())
            cmd_struct.cmd[i] = cmd[i];
        else
            cmd_struct.cmd[i] = '\0';
}

#endif //DISTRIBUTED_FILES_STORAGE_HELPER_H
