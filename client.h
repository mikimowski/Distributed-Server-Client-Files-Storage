#ifndef DISTRIBUTED_FILES_STORAGE_CLIENT_H
#define DISTRIBUTED_FILES_STORAGE_CLIENT_H

#include <iostream>
#include <chrono>
#include <random>
#include <unordered_map>
#include <fstream>
#include <thread>
#include <atomic>

#include <boost/program_options.hpp>
#include <boost/algorithm/string.hpp>
#include <boost/filesystem.hpp>
#include <boost/format.hpp>
#include <boost/log/core.hpp>
#include <boost/log/trivial.hpp>
#include <boost/log/utility/setup/file.hpp>
#include <boost/log/utility/setup/common_attributes.hpp>

#include <unistd.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <boost/filesystem/path.hpp>

#include "client_configuration.h"
#include "communication_protocol.h"
#include "udp_socket.h"
#include "tcp_socket.h"

#ifdef __APPLE__
#include <libkern/OSByteOrder.h>
#define htobe16(x) OSSwapHostToBigInt16(x)
#define htole16(x) OSSwapHostToLittleInt16(x)
#define be16toh(x) OSSwapBigToHostInt16(x)
#define le16toh(x) OSSwapLittleToHostInt16(x)
#define htobe32(x) OSSwapHostToBigInt32(x)
#define htole32(x) OSSwapHostToLittleInt32(x)
#define be32toh(x) OSSwapBigToHostInt32(x)
#define le32toh(x) OSSwapLittleToHostInt32(x)
#define htobe64(x) OSSwapHostToBigInt64(x)
#define htole64(x) OSSwapHostToLittleInt64(x)
#define be64toh(x) OSSwapBigToHostInt64(x)
#define le64toh(x) OSSwapLittleToHostInt64(x)
#endif	/* __APPLE__ */


class Client {
    struct ServerData {
        uint64_t available_space;
        const char* ip_addr;

        ServerData(const char* ip_addr, uint64_t available_space)
                : ip_addr(ip_addr),
                  available_space(available_space)
        {}
//
//    friend std::ostream& operator<< (std::ostream& out, ServerData &rhs) {
//        out << "IP_ADDR = " << rhs.ip_addr << endl;
//        out << "AVAILABLE_SPACE " << rhs.available_space << endl;
//
//        return out;
//    }

        friend bool operator>(const ServerData& lhs, const ServerData& rhs) {
            return lhs.available_space > rhs.available_space;
        }
    };

    const std::string mcast_addr;
    const uint16_t cmd_port;
    const std::string out_folder;
    const uint16_t timeout;

    std::atomic<int> running_threads = 0;

    udp_socket multicast_sock;

    std::mt19937_64 generator;
    std::uniform_int_distribution<uint64_t> uniform_distribution;

    // For each filename stores last source_ip from which it was received
    std::unordered_map<std::string, std::string> last_search_results;


    uint64_t generate_message_sequence();

    void send_message_multicast_udp(int sock, const SimpleMessage &message, uint16_t data_length = 0);

    void send_message_multicast_udp(int sock, const ComplexMessage &message, uint16_t data_length = 0);

    void send_message_unicast_udp(int sock, const char* destination_ip, const ComplexMessage &message, uint16_t data_length = 0);

    void send_message_unicast_udp(int sock, const char* destination_ip, const SimpleMessage &message, uint16_t data_length = 0);

    template<typename T>
    bool its_timeout(const std::chrono::time_point<T>& start_time) {
        auto curr_time = std::chrono::high_resolution_clock::now();
        std::chrono::duration<double, std::milli> elapsed_time = curr_time - start_time;
        return elapsed_time.count() / 1000 >= timeout;
    }

    template<typename... A>
    void handler(A &&... args) {
        std::thread handler{std::forward<A>(args)...};
        running_threads++;
        handler.detach();
    }

    /*************************************************** DISCOVER *****************************************************/

    /**
     * @param udp_socket
     * @return Expected respond message's message_seq
     */
    uint64_t send_discover_message(udp_socket& sock);

    void receive_discover_response(udp_socket& sock, uint64_t expected_message_seq);

    void discover();

    std::multiset<ServerData, std::greater<>> silent_discover_receive(udp_socket& sock, uint64_t expected_message_seq);

    std::multiset<ServerData, std::greater<>> silent_discover();

    /************************************************ GET FILES LIST **************************************************/

    void display_and_update_files_list(char* data, const std::string& server_ip);

    uint64_t send_get_files_list_message(const std::string& pattern);

    void receive_search_respond(uint64_t expected_message_seq);

    void search(std::string pattern);

    /************************************************** FETCH FILE ****************************************************/
    // TODO jakiś fałszywy server mógłby się podszyć... trzeba srpawdzić czy otrzymano faktycznie od tego...
    in_port_t receive_fetch_file_response(udp_socket& sock, uint64_t expected_message_seq, const std::string& expected_filename);

    // TODO error handlign
    void fetch_file_via_tcp(const std::string& server_ip, in_port_t server_port, const std::string& filename);


    void fetch(std::string filename, const std::string& server_ip);

    /************************************************* UPLOAD FILE ****************************************************/

    std::tuple<in_port_t, bool> can_upload_file(udp_socket& sock, const std::string&  server_ip, const std::string& filename, ssize_t file_size);

    // TODO wredny server fałszyyw moze wyslac wszystko dobrze ale nie być tym serverem...
    std::tuple<in_port_t, bool> receive_upload_file_response(udp_socket& sock, uint64_t expected_message_sequence, const std::string& expected_filename);

    // TODO error handling...
    void upload_file_via_tcp(const char* server_ip, in_port_t server_port, const boost::filesystem::path& file_path);

    static bool at_least_on_server_has_space(const std::multiset<Client::ServerData, std::greater<>>& servers, size_t space_required);

    /**
     * Workflow:
     * 1. Discover servers and save them into the set
     * 2. If there are still not asked servers, ask next one with biggest available space whether file can be uploaded
     * 3. If no repeat this step
     * 4. If servers was found connect via TCP and send file
     * 5. Inform user whether file was successfully uploaded or not
     * @param file
     */
    /*
     * TODO:
     * 1. Validate file ~ exists?
     */
    void upload(boost::filesystem::path file_path);

    /************************************************* REMOVE FILE ****************************************************/
    void remove(std::string filename);

    /***************************************************** EXIT *******************************************************/
    void exit();

public:
    template<typename T>
    void set_socket_timeout2(int sock, const std::chrono::time_point<T>& start_time, uint64_t timeout) { // TODO set timeout...
        auto curr_time = std::chrono::high_resolution_clock::now();
        auto microseconds_passed = std::chrono::duration_cast<std::chrono::microseconds>(curr_time - start_time).count();
        auto seconds_passed = std::chrono::duration_cast<std::chrono::seconds>(curr_time - start_time).count();

        struct timeval timeval{};
        if (microseconds_passed != 0)
            seconds_passed++;
        timeval.tv_sec = timeout - (microseconds_passed / 1e6 + 1);
        timeval.tv_usec = 1e6 - (microseconds_passed % 1e6);
    }
//
//    void tmp() {
//        auto start_time = std::chrono::high_resolution_clock::now();
//        sleep(1);
//        auto curr_time = std::chrono::high_resolution_clock::now();
//        for (int i = 0; i < 1000; i++)
//        {}
//
//        auto tmp = std::chrono::duration_cast<std::chrono::microseconds>(curr_time - start_time).count() % (int)1e6;
//        cout << tmp << endl;
//        cout << 1e6 - tmp << endl;
//    }


    Client(std::string mcast_addr, uint16_t cmd_port, std::string folder, const uint16_t timeout);

    Client(ClientConfiguration& configuration);

    void run();

    void init();

    friend std::ostream& operator << (std::ostream &out, const Client &client);
};




#endif //DISTRIBUTED_FILES_STORAGE_CLIENT_H
