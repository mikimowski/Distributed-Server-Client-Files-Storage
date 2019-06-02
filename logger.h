#ifndef DISTRIBUTED_FILES_STORAGE_LOGGER_H
#define DISTRIBUTED_FILES_STORAGE_LOGGER_H

#include <vector>

class logger {
public:
    /**************************************************** DISCOVER ****************************************************/
    static void server_discovery(const char* server_ip, const char* server_mcast_addr, uint64_t server_space);

    /***************************************************** SEARCH *****************************************************/
    static void display_files_list(const std::vector<std::string>& files, const std::string& source_ip);

    /***************************************************** FETCH ******************************************************/
    static void file_fetch_success(const std::string& filename, const std::string& server_ip, uint16_t server_port);
    static void file_fetch_failure(const std::string& filename, const std::string& server_ip,  uint16_t server_port, const std::string& reason);

    /***************************************************** UPLOAD *****************************************************/
    static void file_too_big(const std::string& file);
    static void file_not_exist(const std::string& file);
    static void file_upload_success(const std::string& filename, const std::string& server_ip, uint16_t server_port);
    static void file_upload_failure(const std::string& filename, const std::string& server_ip, uint16_t server_port, const std::string& reason);

    static void package_skipping(const std::string& source_ip, uint16_t source_port, const std::string& reason);

    static void message_cout(const std::string& message);
    static void syserr(const std::string& msg);
};


#endif //DISTRIBUTED_FILES_STORAGE_LOGGER_H
