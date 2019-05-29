#include <iostream>
#include <chrono>
#include <random>
#include <unordered_map>
#include <fstream>
#include <thread>

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

#include "helper.h"
#include "err.h"



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


constexpr uint16_t default_timeout = 5;
constexpr uint16_t max_timeout = 300;

namespace po = boost::program_options;
namespace cp = communication_protocol;
namespace fs = boost::filesystem;
namespace logging = boost::log;

using std::string;
using std::cout;
using std::cin;
using std::endl;
using std::cerr;
using std::to_string;
using std::vector;
using std::string_view;
using std::tuple;
using std::ostream;
using std::multiset;
using std::unordered_map;
using boost::format;

struct ServerData {
    uint64_t available_space;
    const char* ip_addr;

    ServerData(const char* ip_addr, uint64_t available_space)
    : ip_addr(ip_addr),
    available_space(available_space)
    {}

    friend ostream& operator<< (ostream& out, ServerData &rhs) {
        out << "IP_ADDR = " << rhs.ip_addr << endl;
        out << "AVAILABLE_SPACE " << rhs.available_space << endl;

        return out;
    }

    friend bool operator>(const ServerData& lhs, const ServerData& rhs) {
        return lhs.available_space > rhs.available_space;
    }
};

struct ClientSettings {
    string mcast_addr;
    uint16_t cmd_port;
    string out_folder;
    uint16_t timeout;
};

ClientSettings parse_program_arguments(int argc, const char *argv[]) {
    ClientSettings client_settings{};

    po::options_description description {"Program options"};
    description.add_options()
        ("help,h", "Help screen")
        ("MCAST_ADDR,g", po::value<string>(&client_settings.mcast_addr)->required(), "Multicast address")
        ("CMD_PORT,p", po::value<uint16_t>(&client_settings.cmd_port)->required(), "UDP port on which servers are listening")
        ("OUT_FLDR,o", po::value<string>(&client_settings.out_folder)->required(), "Path to the directory in which files should be saved")
        ("TIMEOUT,t", po::value<uint16_t>(&client_settings.timeout)->default_value(default_timeout)->notifier([](uint16_t timeout) {
             if (timeout > max_timeout) {
                 cerr << "TIMEOUT out of range\n";
                 exit(1);
             }
        }),
         (("Maximum waiting time for information from servers\n"
           "  Min value: 0\n"
           "  Max value: " + to_string(max_timeout)) + "\n" +
           "  Default value: " + to_string(default_timeout)).c_str());

    po::variables_map var_map;
    try {
        po::store(po::parse_command_line(argc, argv, description), var_map);
        if (var_map.count("help")) {
            cout << description << endl;
            exit(0);
        }
        po::notify(var_map);
    } catch (po::required_option &e) {
        cerr << e.what() << endl;
        exit(1);
    }

    return client_settings;
}


class Client {
    const string mcast_addr;
    const uint16_t cmd_port;
    const string out_folder;
    const uint16_t timeout;

    std::mt19937_64 generator;
    std::uniform_int_distribution<uint64_t> uniform_distribution;

    // For each filename stores last source_ip from which it was received
    unordered_map<string, string> last_search_results;

    static int create_multicast_udp_socket() {
        int mcast_udp_socket, optval;

        if ((mcast_udp_socket = socket(AF_INET, SOCK_DGRAM, 0)) < 0)
            syserr("socket");

        /* uaktywnienie rozgłaszania (ang. broadcast) */
        optval = 1;
        if (setsockopt(mcast_udp_socket, SOL_SOCKET, SO_BROADCAST, (void*)&optval, sizeof optval) < 0)
            syserr("setsockopt broadcast");

        /* ustawienie TTL dla datagramów rozsyłanych do grupy */
        optval = TTL;
        if (setsockopt(mcast_udp_socket, IPPROTO_IP, IP_MULTICAST_TTL, (void*)&optval, sizeof optval) < 0)
            syserr("setsockopt multicast ttl");

        return mcast_udp_socket;
    }

    int create_unicast_udp_socket() {
        int unicast_socket;
        if ((unicast_socket = socket(AF_INET, SOCK_DGRAM, 0)) < 0)
            syserr("socket");
        return unicast_socket;
    }

    static int create_and_connect_tcp_socket(const char* destination_ip, uint16_t destination_port) {
        int tcp_socket;
        struct sockaddr_in destination_address{};
        memset(&destination_address, 0, sizeof(destination_address));
        destination_address.sin_family = AF_INET;
        destination_address.sin_port = htobe16(destination_port);
        if (inet_aton(destination_ip, &destination_address.sin_addr) == 0)
            syserr("inet_aton");
        if ((tcp_socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)) < 0)
            syserr("socket");
        if (connect(tcp_socket, (struct sockaddr*) &destination_address, sizeof(destination_address)) < 0)
            syserr("connect");

        return tcp_socket;
    }

    uint64_t generate_message_sequence() {
        return uniform_distribution(generator);
    }

    // TODO jakoś lepiej...
    static bool is_expected_command(const char* command, const string& expected_command) {
        int i = 0;
        for (; i < expected_command.length(); i++)
            if (command[i] != expected_command[i])
                return false;
        while (i < const_variables::max_command_length)
            if (command[i++] != '\0')
                return false;
        return true;
    }
//
//    static bool is_valid_message(SimpleMessage message, uint64_t expected_message_seq) {
//        if (be64toh(message.message_seq) != expected_message_seq)
//            return false;
//        if (!is_valid_string(message.data, const_variables::max_simple_data_size))
//            return false;
//        return true;
//    }
//
//    static bool is_valid_message(ComplexMessage message, uint64_t expected_message_seq) {
//        if (be64toh(message.message_seq) != expected_message_seq)
//            return false;
//        if (!is_valid_string(message.data, const_variables::max_complex_data_size))
//            return false;
//        return true;
//    }

    static void message_validation(const ComplexMessage& message, uint64_t expected_message_seq, const string& expected_command, ssize_t message_size, const string& expected_data = "") {
        if (message_size < const_variables::complex_message_no_data_size)
            throw invalid_message("message too small");
        if (!is_expected_command(message.command, expected_command))
            throw invalid_message("invalid command");
        if (be64toh(message.message_seq) != expected_message_seq)
            throw invalid_message("invalid message seq");
        if (!is_valid_data(message.data, message_size - const_variables::complex_message_no_data_size)) //  TODO is it right? check!
            throw invalid_message("invalid message data");
    }

    static void message_validation(const SimpleMessage& message, uint64_t expected_message_seq, const string& expected_command, ssize_t message_size, const string& expected_data = "") {
        if (message_size < const_variables::simple_message_no_data_size)
            throw invalid_message("message too small");
        if (!is_expected_command(message.command, expected_command))
            throw invalid_message("invalid command");
        if (be64toh(message.message_seq) != expected_message_seq)
            throw invalid_message("invalid message seq");
        if (!is_valid_data(message.data, message_size - const_variables::simple_message_no_data_size)) //  TODO is it right? check!
            throw invalid_message("invalid message data");
        if (!expected_data.empty() && expected_data != message.data)
            throw invalid_message("unexpected data received");

    }


    void send_message_multicast_udp(int sock, const SimpleMessage &message, uint16_t data_length = 0) {
        uint16_t message_length = const_variables::simple_message_no_data_size + data_length;
        struct sockaddr_in destination_address{};
        destination_address.sin_family = AF_INET;
        destination_address.sin_port = htons(cmd_port);
        if (inet_aton(mcast_addr.c_str(), &destination_address.sin_addr) == 0)
            syserr("inet_aton");
        if (sendto(sock, &message, message_length, 0, (struct sockaddr*) &destination_address, sizeof(destination_address)) != message_length)
            syserr("sendto");

        BOOST_LOG_TRIVIAL(info) << "multicast udp message sent";
    }

    void send_message_multicast_udp(int sock, const ComplexMessage &message, uint16_t data_length = 0) {
        uint16_t message_length = const_variables::complex_message_no_data_size + data_length;
        struct sockaddr_in destination_address{};
        destination_address.sin_family = AF_INET;
        destination_address.sin_port = htons(cmd_port);
        if (inet_aton(mcast_addr.c_str(), &destination_address.sin_addr) == 0)
            syserr("inet_aton");
        if (sendto(sock, &message, message_length, 0, (struct sockaddr*) &destination_address, sizeof(destination_address)) != message_length)
            syserr("sendto");

        BOOST_LOG_TRIVIAL(info) << "multicast udp message sent";
    }

    void send_message_unicast_udp(int sock, const char* destination_ip, const ComplexMessage &message, uint16_t data_length = 0) {
        uint16_t message_length = const_variables::complex_message_no_data_size + data_length;
        struct sockaddr_in destination_address{};
        destination_address.sin_family = AF_INET;
        destination_address.sin_port = htons(cmd_port);
        if (inet_aton(destination_ip, &destination_address.sin_addr) == 0)
            syserr("inet_aton");
        if (sendto(sock, &message, message_length, 0, (struct sockaddr*) &destination_address, sizeof(destination_address)) != message_length)
            syserr("sendto");

        BOOST_LOG_TRIVIAL(info) << "unicast udp message sent";
    }

    void send_message_unicast_udp(int sock, const char* destination_ip, const SimpleMessage &message, uint16_t data_length = 0) {
        uint16_t message_length = const_variables::simple_message_no_data_size + data_length;
        struct sockaddr_in destination_address{};
        destination_address.sin_family = AF_INET;
        destination_address.sin_port = htons(cmd_port);
        if (inet_aton(destination_ip, &destination_address.sin_addr) == 0)
            syserr("inet_aton");
        if (sendto(sock, &message, message_length, 0, (struct sockaddr*) &destination_address, sizeof(destination_address)) != message_length)
            syserr("sendto");

        BOOST_LOG_TRIVIAL(info) << "unicast udp message sent";
    }

    template<typename T>
    bool its_timeout(const std::chrono::time_point<T>& start_time) {
        auto curr_time = std::chrono::high_resolution_clock::now();
        std::chrono::duration<double, std::milli> elapsed_time = curr_time - start_time;
        return elapsed_time.count() / 1000 >= timeout;
    }

    // TODO throw expection if failed... ?? no way of failure...
    static tuple<string, in_port_t> unpack_sockaddr(const struct sockaddr_in& address) {
        return {inet_ntoa(address.sin_addr), be16toh(address.sin_port)};
    }

    /*************************************************** DISCOVER *****************************************************/

    static void display_server_discovered_info(const char* server_ip, const char* server_mcast_addr, uint64_t server_space) {
        cout << format("Found %1%(%2%) with free space %3%\n") %server_ip %server_mcast_addr %server_space;
    }

    /**
     * @param udp_socket
     * @return Expected respond message's message_seq
     */
    uint64_t send_discover_message(int udp_socket) {
        uint64_t message_seq = generate_message_sequence();
        SimpleMessage message {htobe64(message_seq), cp::discover_request};
        send_message_multicast_udp(udp_socket, message);
        return message_seq;
    }

    void receive_discover_response(int udp_socket, uint64_t expected_message_seq) {
        struct ComplexMessage message_received {};
        struct sockaddr_in source_address {};
        socklen_t addr_len;
        ssize_t recv_len;

        set_socket_timeout(udp_socket);
        auto start_time = std::chrono::high_resolution_clock::now();
        while (!its_timeout(start_time)) {
            addr_len = sizeof(struct sockaddr_in);
            recv_len = recvfrom(udp_socket, &message_received, sizeof(struct ComplexMessage), 0,
                                (struct sockaddr *) &source_address, &addr_len);
            if (recv_len >= 0) {
                try {
                    message_validation(message_received, expected_message_seq, cp::discover_response, recv_len);
                    display_server_discovered_info(inet_ntoa(source_address.sin_addr), message_received.data,
                                                   be64toh(message_received.param));
                } catch (const invalid_message &e) {
                    auto[source_ip, source_port] = unpack_sockaddr(source_address);
                    cerr << format("[PCKG ERROR] Skipping invalid package from %1%:%2%. %3%\n") %source_ip %source_port %e.what();
                    BOOST_LOG_TRIVIAL(error) << format("[PCKG ERROR] Skipping invalid package from %1%:%2%. %3%\n") %source_ip %source_port %e.what();
                }
            }
        }
    }

    void discover() {
        BOOST_LOG_TRIVIAL(trace) << "Starting discover...";
        int udp_socket = create_multicast_udp_socket();
        uint64_t expected_message_seq = send_discover_message(udp_socket);
        receive_discover_response(udp_socket, expected_message_seq);
        if (close(udp_socket) < 0)
            syserr("close");
        BOOST_LOG_TRIVIAL(trace) << "Finished discover";
    }

    multiset<ServerData, std::greater<>> silent_discover_receive(int udp_socket, uint64_t expected_message_seq) {
        multiset<ServerData, std::greater<>> servers;
        struct ComplexMessage message{};
        struct sockaddr_in source_address{};
        socklen_t addr_len;
        ssize_t recv_len;

        set_socket_timeout(udp_socket);
        auto start_time = std::chrono::high_resolution_clock::now();
        while (!its_timeout(start_time)) {
            addr_len = sizeof(struct sockaddr_in);
            recv_len = recvfrom(udp_socket, &message, sizeof(struct ComplexMessage), 0,
                                (struct sockaddr *) &source_address, &addr_len);
            if (recv_len >= 0) {
                try {
                    message_validation(message, expected_message_seq, cp::discover_response, recv_len);
                    servers.insert(ServerData(inet_ntoa(source_address.sin_addr), be64toh(message.param)));
                } catch (const invalid_message &e) {
                    auto[source_ip, source_port] = unpack_sockaddr(source_address);
                    cerr << format("[PCKG ERROR] Skipping invalid package from %1%:%2%. %3%\n") %source_ip %source_port %e.what();
                    BOOST_LOG_TRIVIAL(error) << format("[PCKG ERROR] Skipping invalid package from %1%:%2%. %3%\n") %source_ip %source_port %e.what();
                }
            }
        }

        return servers;
    }

    multiset<ServerData, std::greater<>> silent_discover() {
        int udp_socket = create_multicast_udp_socket();
        uint64_t expected_message_seq = send_discover_message(udp_socket);
        auto servers = silent_discover_receive(udp_socket, expected_message_seq);
        if (close(udp_socket) < 0)
            syserr("close");

        return servers;
    }

    /************************************************ GET FILES LIST **************************************************/

    void display_and_update_files_list(char *data, uint32_t list_length, const char *server_ip) {
        char *filename = strtok(data, "\n");
        while (filename != nullptr) {
            cout << filename << " (" << server_ip << ")" << endl;
            this->last_search_results[filename] = server_ip;
            filename = strtok(nullptr, "\n");
        }
    }

    uint64_t send_get_files_list_message(int udp_socket, const string &pattern) {
        SimpleMessage message{htobe64(generate_message_sequence()), cp::files_list_request, pattern.c_str()};
        send_message_multicast_udp(udp_socket, message, pattern.length());
    }

    void receive_search_respond(int udp_socket, uint64_t expected_message_seq) {
        struct SimpleMessage message{};
        struct sockaddr_in source_address{};
        socklen_t addr_len;
        ssize_t recv_len;

        set_socket_timeout(udp_socket);
        auto start_time = std::chrono::high_resolution_clock::now();
        while (!its_timeout(start_time)) {
            addr_len = sizeof(struct sockaddr_in);
            recv_len = recvfrom(udp_socket, &message, sizeof(message), 0, (struct sockaddr*)&source_address, &addr_len);
            if (recv_len >= 0) {
                auto [source_ip, source_port] = unpack_sockaddr(source_address);

                try {
                    message_validation(message, expected_message_seq, cp::discover_response, recv_len);
                    display_and_update_files_list(message.data, recv_len - const_variables::simple_message_no_data_size, source_ip.c_str());
                } catch (const invalid_message &e) {
                    cerr << format("[PCKG ERROR] Skipping invalid package from %1%:%2%. %3%\n") %source_ip %source_port %e.what();
                    BOOST_LOG_TRIVIAL(error) << format("[PCKG ERROR] Skipping invalid package from %1%:%2%. %3%\n") %source_ip %source_port %e.what();
                }
            }
        }
    }

    void search(string pattern) {
        BOOST_LOG_TRIVIAL(info) << format("Starting search, pattern=%1%...") %pattern;
        int udp_socket = create_multicast_udp_socket();
        uint64_t message_seq = send_get_files_list_message(udp_socket, pattern);
        receive_search_respond(udp_socket, message_seq);
        if (close(udp_socket) < 0)
            syserr("close");
        BOOST_LOG_TRIVIAL(info) << "Search finished";
    }

    /************************************************** FETCH FILE ****************************************************/
    uint64_t send_get_file_message(int udp_socket, const char* destination_ip, const string &filename) {
        uint64_t message_sequence = generate_message_sequence();
        SimpleMessage message{htobe64(message_sequence), cp::file_get_request, filename.c_str()};
        send_message_unicast_udp(udp_socket, destination_ip, message, filename.length());
        return message_sequence;
    }

    // TODO jakiś fałszywy server mógłby się podszyć... trzeba srpawdzić czy otrzymano faktycznie od tego...
    in_port_t receive_fetch_file_response(int udp_socket, uint64_t expected_message_seq, const string& expected_filename) {
        struct ComplexMessage message{};
        struct sockaddr_in source_address{};
        socklen_t addr_len;
        ssize_t recv_len;

        set_socket_timeout(udp_socket);
        auto start_time = std::chrono::high_resolution_clock::now();
        while (!its_timeout(start_time)) {
            addr_len = sizeof(struct sockaddr_in);
            recv_len = recvfrom(udp_socket, &message, sizeof(message), 0, (struct sockaddr *) &source_address, &addr_len);

            if (recv_len >= 0) {
                auto[source_ip, source_port] = unpack_sockaddr(source_address);

                try {
                    message_validation(message, expected_message_seq, cp::file_get_response, recv_len, expected_filename);
                    BOOST_LOG_TRIVIAL(info)
                        << format("fetch file response received: port=%1% filename=%2%, source=%3%:%4%")
                           %be64toh(message.param) %message.data %source_ip %source_port;
                } catch (const invalid_message &e) {
                    cerr << format("[PCKG ERROR] Skipping invalid package from %1%:%2%. %3%\n") %source_ip %source_port %e.what();
                    BOOST_LOG_TRIVIAL(error) << format("[PCKG ERROR] Skipping invalid package from %1%:%2%. %3%\n") %source_ip %source_port %e.what();
                }
            }
        }

        return be64toh(message.param);
    }

    // TODO error handlign
    void fetch_file_via_tcp(const string& server_ip, in_port_t server_port, const string& filename) {
        BOOST_LOG_TRIVIAL(trace) << "Starting downloading file via tcp...";
        int tcp_socket = create_and_connect_tcp_socket(server_ip.c_str(), server_port);
        size_t recv_len;

        fs::path file_path(this->out_folder + filename);
        fs::ofstream file_stream(file_path, std::ofstream::binary);
        if (file_stream.is_open()) {
            char buffer[MAX_BUFFER_SIZE];
            while ((recv_len = read(tcp_socket, buffer, sizeof(buffer))) > 0) {
                file_stream.write(buffer, recv_len);
            }
            file_stream.close();
        } else {
            cerr << "File creation failure" << endl;
        }

        if (close(tcp_socket) < 0)
            syserr("close");
        BOOST_LOG_TRIVIAL(trace) << "Downloading file via tcp finished";
    }


    void fetch(string filename, const string& server_ip) {
        BOOST_LOG_TRIVIAL(trace) << format("Starting fetch, filename = %1%") % filename;
        int unicast_socket = create_unicast_udp_socket();
        uint64_t expected_message_seq = send_get_file_message(unicast_socket, server_ip.c_str(), filename);
        in_port_t tcp_port = receive_fetch_file_response(unicast_socket, expected_message_seq, filename);
        fetch_file_via_tcp(server_ip, tcp_port, filename);
        if (close(unicast_socket) < 0)
            syserr("close");

        BOOST_LOG_TRIVIAL(trace) << "Ending fetch";
    }

    /************************************************* UPLOAD FILE ****************************************************/

    bool can_upload_file(int udp_socket, const char* server_ip, const string& filename, ComplexMessage& server_response) {
        uint64_t message_sequence = send_upload_file_request(udp_socket, server_ip, filename);
        return receive_upload_file_response(udp_socket, message_sequence, server_response, filename);
    }

    /// return sent message's message_sequence
    uint64_t send_upload_file_request(int udp_socket, const char *destination_ip, const string &filename) {
        uint64_t message_sequence = generate_message_sequence();
        ComplexMessage message{htobe64(message_sequence), cp::file_add_request, filename.c_str(), htobe64(get_file_size(this->out_folder + filename))};
        send_message_unicast_udp(udp_socket, destination_ip, message, filename.length());
        return message_sequence;
    }


    // TODO wredny server fałszyyw moze wyslac wszystko dobrze ale nie być tym serverem...
    bool receive_upload_file_response(int udp_socket, uint64_t expected_message_sequence, ComplexMessage& message, const string& expected_filename) {
        struct sockaddr_in source_address {};
        socklen_t addr_len;
        ssize_t recv_len;

        set_socket_timeout(udp_socket);
        auto start_time = std::chrono::high_resolution_clock::now();
        while (!its_timeout(start_time)) {
            addr_len = sizeof(struct sockaddr_in);
            recv_len = recvfrom(udp_socket, &message, sizeof(struct ComplexMessage), 0, (struct sockaddr *) &source_address, &addr_len);
            if (recv_len >= 0) {
                try { // TODO check if received filename == expected filename
                    if (is_expected_command(message.command, cp::file_add_acceptance)) {
                        message_validation(message, expected_message_sequence, cp::file_add_acceptance, recv_len);
                        return true; // connect_me
                    } else {
                        auto message_simple = (SimpleMessage *) &message;
                        message_validation(*message_simple, expected_message_sequence, cp::file_add_refusal, recv_len, expected_filename);
                        return false; // no_way
                    }
                } catch (const invalid_message &e) {
                    auto [source_ip, source_port] = unpack_sockaddr(source_address);
                    cerr << format("[PCKG ERROR] Skipping invalid package from %1%:%2%.") % source_ip % source_port;
                    BOOST_LOG_TRIVIAL(error)
                        << format("[PCKG ERROR] Skipping invalid package from %1%:%2%. %3%") % source_ip %
                           source_port % e.what();
                }
            }
        }

/// FALSE -> no valid response received
        return false;
    }

    // TODO error handling...
    void upload_file_via_tcp(const char* server_ip, in_port_t server_port, const fs::path& file_path) {
        BOOST_LOG_TRIVIAL(trace) << "Starting uploading file via tcp...";
        int tcp_socket = create_and_connect_tcp_socket(server_ip, server_port);

        std::ifstream file_stream {file_path.c_str(), std::ios::binary};
        if (file_stream.is_open()) {
            char buffer[MAX_BUFFER_SIZE];
            while (file_stream) {
                file_stream.read(buffer, MAX_BUFFER_SIZE);
                ssize_t length = file_stream.gcount();
                if (write(tcp_socket, buffer, length) != length)
                    syserr("partial write");
            }
        } else {
            cerr << "File opening error" << endl; // TODO
        }
        file_stream.close(); // TODO can throw

        if (close(tcp_socket) < 0)
            syserr("close");
        BOOST_LOG_TRIVIAL(trace) << "Uploading file via tcp finished";
        cout << format("File %1% uploaded (%2%:%3%)\n") %file_path.filename() %server_ip %server_port;
    }

    static bool at_least_on_server_has_space(const multiset<ServerData, std::greater<>>& servers, size_t space_required) {
        return !servers.empty() && servers.begin()->available_space >= space_required;
    }

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
    void upload(fs::path file_path) { // copy because it's another thread!
        BOOST_LOG_TRIVIAL(trace) << "Starting upload procedure...";
        ComplexMessage server_response{};

        if (!fs::exists(file_path) || !fs::is_regular_file(file_path)) {
            BOOST_LOG_TRIVIAL(trace) << format("Invalid file, file_path = %1%") %file_path;
        } else {
            string filename = file_path.filename().string();
            multiset<ServerData, std::greater<>> servers = silent_discover();
            int udp_socket = create_multicast_udp_socket();
            if (at_least_on_server_has_space(servers, fs::file_size(file_path))) {
                for (const auto &server : servers) {
                    if (can_upload_file(udp_socket, server.ip_addr, filename, server_response)) {
                        upload_file_via_tcp(server.ip_addr, htobe64(server_response.param), file_path);
                        return;
                    }
                }
            }
        }

        cout << format("File %1% too big %1%\n") %file_path;

        BOOST_LOG_TRIVIAL(trace) << "Ending upload procedure...";
    }

    /************************************************* REMOVE FILE ****************************************************/
    void remove(string filename) {
        BOOST_LOG_TRIVIAL(trace) << "Starting remove file";
        SimpleMessage message {htobe64(generate_message_sequence()), cp::file_remove_request, filename.c_str()};
        int udp_socket = create_multicast_udp_socket(); // TODO i tu ładny try catch na tworzenie ;)
        send_message_multicast_udp(udp_socket, message, filename.length());
        if (close(udp_socket))
            syserr("close");
        BOOST_LOG_TRIVIAL(trace) << "Ending remove file";
    }

    /***************************************************** EXIT *******************************************************/
    void exit() {
        BOOST_LOG_TRIVIAL(trace) << "Exit";
        std::exit(0);
    }

public:
//    template<typename T>
//    void set_socket_timeout2(int sock, const std::chrono::time_point<T>& start_time, uint64_t timeout) { // TODO set timeout...
//        auto curr_time = std::chrono::high_resolution_clock::now();
//        auto microseconds_passed = std::chrono::duration_cast<std::chrono::microseconds>(curr_time - start_time).count();
//        auto seconds_passed = std::chrono::duration_cast<std::chrono::seconds>(curr_time - start_time).count();
//
//        struct timeval timeval{};
//        if (microseconds_passed != 0)
//            seconds_passed++;
//        timeval.tv_sec = timeout - (microseconds_passed / 1e6 + 1);
//        timeval.tv_usec = 1e6 - (microseconds_passed % 1e6);
//    }
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


    Client(string mcast_addr, uint16_t cmd_port, string folder, const uint16_t timeout)
            : mcast_addr(std::move(mcast_addr)),
              cmd_port(cmd_port),
              out_folder(std::move(folder)),
              timeout(timeout),
              generator(std::random_device{}())
    {}

    Client(ClientSettings& settings)
            : Client(settings.mcast_addr.c_str(), settings.cmd_port, settings.out_folder, settings.timeout)
    {}



    static bool is_param_required(const string& command) {
        std::set<string> s = {"fetch", "upload", "remove"};
        return s.find(command) != s.end();
    }

    static bool no_parameter_allowed(const string& command) {
        std::set<string> s {"exit", "discover"};
        return s.find(command) != s.end();
    }


    /**
     *
     * @return <command, parameter> command and parameter given by user, if parameter is optional and wasn't given then empty string is returned
     */
    static tuple<string, string> read_user_command() {
        vector<string> tokenized_input;
        string input, command, param = "";
        getline(cin, input);
        boost::split(tokenized_input, input, [](char c) {
            return iswspace(c);
        },
        boost::token_compress_on);

        if (tokenized_input.empty())
            throw invalid_user_input("no command inserted");
        else if (tokenized_input.size() >= 2 && no_parameter_allowed(tokenized_input[0]))
            throw invalid_user_input("too many parameters");
        else if (tokenized_input.size() == 1 && is_param_required(tokenized_input[0]))
            throw invalid_user_input("command required parameter");

        command = tokenized_input[0];
        tokenized_input.erase(tokenized_input.begin());
        if (!tokenized_input.empty())
            param = boost::algorithm::join(tokenized_input, " ");
        return {command, param};
    }

    void run() {
        string comm, param;
        bool exit = false;

        while (!exit) {
            display_log_separator();
            cout << "Enter command: " << endl;
            // TODO: variable: "running", thread checks wheter it's true, if not then it should terminate
            try {
                auto[comm, param] = read_user_command();
                boost::algorithm::to_lower(comm);

                if (comm == "exit") {
                    exit = true;
                } else if (comm == "discover") {
                    discover();
                } else {
                    if (comm == "search") {
                        search(param);
                    } else if (comm == "fetch") {
                        if (last_search_results.find(param) == last_search_results.end())
                            cout << "Unknown filename\n";
                        else
                            handler(&Client::fetch, this, param, last_search_results[param]);
                    } else if (comm == "upload") {
                        handler(&Client::upload, this, param);
                    } else if (comm == "remove") {
                        remove(param);
                    } else {
                        throw invalid_user_input("unknown command");
                    }
                }
            } catch (const invalid_user_input& e) {
                BOOST_LOG_TRIVIAL(info) << "invalid user command";
            }
        }
    }

    void init() {

    }

    friend ostream& operator << (ostream &out, const Client &client) {
        out << "\nCLIENT INFO:";
        out << "\nMCAST_ADDR = " << client.mcast_addr;
        out << "\nCMD_PORT = " << client.cmd_port;
        out << "\nFOLDER = " << client.out_folder;
        out << "\nTIMEOUT = " << client.timeout;

        return out;
    }
};

void init() {
    logging::register_simple_formatter_factory<logging::trivial::severity_level, char>("Severity");
    logging::add_file_log
            (
                    logging::keywords::file_name = "logger_client.log",
                    logging::keywords::rotation_size = 10 * 1024 * 1024,
                    logging::keywords::time_based_rotation = logging::sinks::file::rotation_at_time_point(0, 0, 0),
                    logging::keywords::format = "[%TimeStamp%] [tid=%ThreadID%] [%Severity%]: %Message%",
                    logging::keywords::auto_flush = true
            );
    logging::add_common_attributes();
}

int main(int argc, const char *argv[]) {
    init();

    ClientSettings client_settings = parse_program_arguments(argc, argv);
    Client client {client_settings};
    client.init();
    BOOST_LOG_TRIVIAL(trace) << client << endl;
    client.run();

    return 0;
}

