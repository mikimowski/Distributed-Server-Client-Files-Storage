#ifndef DISTRIBUTED_FILES_STORAGE_SERVER_H
#define DISTRIBUTED_FILES_STORAGE_SERVER_H

#include <string>
#include <set>
#include <netinet/in.h>
#include <tuple>
#include <condition_variable>
#include <thread>
#include <atomic>

#include "server_configuration.h"
#include "communication_protocol.h"
#include "udp_socket.h"
#include "tcp_socket.h"

class Server {
    const std::string multicast_address = "";
    in_port_t cmd_port = -1;
    std::mutex used_space_mutex;
    std::atomic<uint64_t> used_space = 0;
    const uint64_t max_available_space = 0;
    const std::string shared_folder = "";
    const uint16_t timeout = 0;

    std::mutex files_in_storage_mutex;
    std::set<std::string> files_in_storage;

    /*** WORKFLOW ***/
    std::atomic<bool> server_running = false;

    /*** Receiving ***/
    udp_socket recv_socket;
    udp_socket send_socket;

    /// Returns true if element was successuly added, false otherwise
    bool add_file_to_storage(const std::string &filename);

    /**
  * Removes filename from set of filenames in storage
  * @param filename filename to be removed from set
  * @return Number of elements erased
  */
    int remove_file_from_storage(const std::string &filename);

    bool is_in_storage(const std::string &filename);

    void generate_files_in_storage();

    void init_sockets();

    uint64_t get_available_space();

    /**
     * Attempts to reserve given amount of space
     * @param size size of space to be reserved
     * @return True if space was reserved successfuly, false otherwise.
     */
    bool check_and_reserve_space(uint64_t size);

    /**
     * Use it correctly!
     */
    void free_space(uint64_t size);

    void try_send_message(const SimpleMessage& message, const struct sockaddr_in& destination_address, uint16_t length);

    void try_send_message(const ComplexMessage& message, const struct sockaddr_in& destination_address, uint16_t length);

    template<typename... A>
    void handler(A &&... args) {
        std::thread handler{std::forward<A>(args)...};
        handler.detach();
    }

    /*************************************************** DISCOVER *****************************************************/

    void handle_discover_request(const struct sockaddr_in &destination_address, uint64_t message_seq);

    /************************************************** FILES LIST ****************************************************/

    void handle_files_list_request(struct sockaddr_in destination_address, uint64_t message_seq, std::string pattern);

    /************************************************* DOWNLOAD FILE **************************************************/

    void handle_file_request(struct sockaddr_in destination_address, uint64_t message_seq, std::string filename);

    void send_file_via_tcp(tcp_socket& tcp_sock, const std::string& filename);

    /**************************************************** UPLOAD ******************************************************/

    void handle_upload_request(struct sockaddr_in destination_address, uint64_t message_seq,
                               std::string filename, uint64_t file_size);

    void upload_file_via_tcp(tcp_socket& tcp_sock, const std::string &filename);

    /**************************************************** REMOVE ******************************************************/

    void handle_remove_request(std::string filename);

    /****************************************************** RUN *******************************************************/

public:
    Server(std::string mcast_addr, in_port_t cmd_port, uint64_t max_available_space, std::string shared_folder_path, uint16_t timeout);

    Server(const ServerConfiguration &server_configuration);

    Server() = default;

    void init();

    void run();

    void stop();

    bool no_threads_running();

    bool stopped();

    friend std::ostream &operator<<(std::ostream &out, const Server &server);
};


#endif //DISTRIBUTED_FILES_STORAGE_SERVER_H

