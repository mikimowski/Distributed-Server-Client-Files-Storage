#ifndef DISTRIBUTED_FILES_STORAGE_CLIENT_H
#define DISTRIBUTED_FILES_STORAGE_CLIENT_H

#include <chrono>
#include <random>
#include <netinet/in.h>
#include <unordered_map>
#include <boost/filesystem/path.hpp>

#include "client_configuration.h"
#include "../protocol/communication_protocol.h"
#include "../socket/udp_socket.h"
#include "../socket/tcp_socket.h"

class Client {
    struct ServerData {
        const char *ip_addr;
        uint64_t available_space;

        ServerData(const char *ip_addr, uint64_t available_space)
                : ip_addr(ip_addr),
                  available_space(available_space) {}

        friend bool operator>(const ServerData &lhs, const ServerData &rhs) {
            return lhs.available_space > rhs.available_space;
        }
    };

    const std::string mcast_addr;
    const uint16_t cmd_port;
    const std::string out_folder;
    const uint16_t timeout;

    udp_socket multicast_sock;

    std::mt19937_64 generator;
    std::uniform_int_distribution<uint64_t> uniform_distribution;

    /// For each filename received stores last source ip (where this file is was available).
    std::unordered_map<std::string, std::string> last_search_results;

    uint64_t generate_message_sequence();

    /*************************************************** DISCOVER *****************************************************/
    void receive_discover_response(udp_socket &sock, uint64_t expected_message_seq);

    void discover();

    std::vector<ServerData> silent_discover_receive(udp_socket &sock, uint64_t expected_message_seq);

    std::vector<ServerData> silent_discover();

    /************************************************ GET FILES LIST **************************************************/
    void update_search_results(const std::vector<std::string>& files, const std::string& server_ip);

    void receive_search_respond(uint64_t expected_message_seq);

    void search(std::string pattern);

    /************************************************** FETCH FILE ****************************************************/

    /***
     * @return 0 if no valid message was received, otherwise port on which server is waiting.
     */
    in_port_t receive_fetch_file_response(udp_socket &sock,
            uint64_t expected_message_seq, const std::string &expected_filename);

    void fetch_file_via_tcp(const std::string &server_ip, in_port_t server_port, const std::string &filename);

    void fetch(std::string filename, const std::string &server_ip);

    /************************************************* UPLOAD FILE ****************************************************/
    std::tuple<in_port_t, bool>
    can_upload_file(udp_socket &sock, const std::string &server_ip, const std::string &filename, ssize_t file_size);

    std::tuple<in_port_t, bool> receive_upload_file_response(udp_socket &sock, uint64_t expected_message_sequence,
                                                             const std::string &expected_filename);

    static void upload_file_via_tcp(const char *server_ip, in_port_t server_port, const boost::filesystem::path &file_path, uint64_t file_size);

    static bool at_least_on_server_has_space(const std::vector<Client::ServerData> &servers, size_t space_required);

    void upload(boost::filesystem::path file_path);

    /************************************************* REMOVE FILE ****************************************************/
    void remove(const std::string& filename);

public:
    Client(std::string mcast_addr, uint16_t cmd_port, std::string folder, uint16_t timeout);

    explicit Client(ClientConfiguration &configuration);

    void run();

    void init();

    friend std::ostream &operator<<(std::ostream &out, const Client &client);
};

#endif //DISTRIBUTED_FILES_STORAGE_CLIENT_H
