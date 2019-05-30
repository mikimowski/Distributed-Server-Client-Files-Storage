#ifndef DISTRIBUTED_FILES_STORAGE_CLIENT_CONFIGURATION_H
#define DISTRIBUTED_FILES_STORAGE_CLIENT_CONFIGURATION_H

namespace client_configuration {
    constexpr uint16_t default_timeout = 5;
    constexpr uint16_t max_timeout = 300;
}

struct ClientConfiguration {
    std::string mcast_addr = "";
    in_port_t cmd_port = 0;
    std::string out_folder = "";
    uint16_t timeout = 0;
};

#endif //DISTRIBUTED_FILES_STORAGE_CLIENT_CONFIGURATION_H
