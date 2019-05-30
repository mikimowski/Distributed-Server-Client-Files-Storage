#ifndef DISTRIBUTED_FILES_STORAGE_SERVER_CONFIGURATION_H
#define DISTRIBUTED_FILES_STORAGE_SERVER_CONFIGURATION_H

namespace server_configuration {
    constexpr uint16_t default_timeout = 5;
    constexpr uint16_t max_timeout = 300;
    constexpr uint64_t default_max_disc_space = 52428800;
}

struct ServerConfiguration {
    std::string mcast_addr = "";
    in_port_t cmd_port = 0;
    uint64_t max_space = 0;
    std::string shared_folder = "";
    uint16_t timeout = 0;
};


#endif //DISTRIBUTED_FILES_STORAGE_SERVER_CONFIGURATION_H
