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
    const char* mcast_addr;
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

    void message_validation(const ComplexMessage& message, uint64_t expected_message_seq, const string& expected_command, ssize_t message_size) {
        if (message_size < const_variables::complex_message_no_data_size)
            throw invalid_message("message too small");
        if (!is_expected_command(message.command, expected_command))
            throw invalid_message("invalid command");
        if (be64toh(message.message_seq) != expected_message_seq)
            throw invalid_message("invalid message seq");
        if (!is_valid_data(message.data, message_size - const_variables::complex_message_no_data_size)) //  TODO is it right? check!
            throw invalid_message("invalid message data");
    }

    void message_validation(const SimpleMessage& message, uint64_t expected_message_seq, const string& expected_command, ssize_t message_size) {
        if (message_size < const_variables::simple_message_no_data_size)
            throw invalid_message("message too small");
        if (!is_expected_command(message.command, expected_command))
            throw invalid_message("invalid command");
        if (be64toh(message.message_seq) != expected_message_seq)
            throw invalid_message("invalid message seq");
        if (!is_valid_data(message.data, message_size - const_variables::complex_message_no_data_size)) //  TODO is it right? check!
            throw invalid_message("invalid message data");
    }


    void send_message_multicast_udp(int sock, const SimpleMessage &message, uint16_t data_length = 0) {
        uint16_t message_length = const_variables::simple_message_no_data_size + data_length;
        struct sockaddr_in destination_address{};
        destination_address.sin_family = AF_INET;
        destination_address.sin_port = htons(cmd_port);
        if (inet_aton(mcast_addr, &destination_address.sin_addr) == 0)
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
        if (inet_aton(mcast_addr, &destination_address.sin_addr) == 0)
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

    /*************************************************** DISCOVER *****************************************************/

    static void display_server_discovered_info(const char* server_ip, const char* server_mcast_addr, uint64_t server_space) {
        cout << "Found " << server_ip << " (" << server_mcast_addr << ") with free space " << server_space << endl;
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
        bool timeout_reached = false;

        set_socket_timeout(udp_socket);
        in_port_t source_port;
        string source_ip;
        auto wait_start_time = std::chrono::high_resolution_clock::now();
        while (!timeout_reached) {
            auto curr_time = std::chrono::high_resolution_clock::now();
            std::chrono::duration<double, std::milli> elapsed_time = curr_time - wait_start_time;
            if (elapsed_time.count() / 1000 >= timeout) {
                timeout_reached = true;
            } else {
                addr_len = sizeof(struct sockaddr_in);
                recv_len = recvfrom(udp_socket, &message_received, sizeof(struct ComplexMessage), 0, (struct sockaddr*) &source_address, &addr_len);
                if (recv_len >= 0) {
                    source_ip = inet_ntoa(source_address.sin_addr);
                    source_port = be16toh(source_address.sin_port);

                    try {
                        message_validation(message_received, expected_message_seq, cp::discover_response, recv_len);
                        display_server_discovered_info(inet_ntoa(source_address.sin_addr), message_received.data, be64toh(message_received.param));
                    } catch (const invalid_message& e) {
                        cerr << format("[PCKG ERROR] Skipping invalid package from %1%:%2%. %3%") %source_ip %source_port %e.what();
                        BOOST_LOG_TRIVIAL(error) << format("[PCKG ERROR] Skipping invalid package from %1%:%2%. %3%") %source_ip %source_port %e.what();
                        continue;
                    }
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
            BOOST_LOG_TRIVIAL(error) << "socket closing";
        BOOST_LOG_TRIVIAL(trace) << "Finished discover";
    }

    multiset<ServerData, std::greater<>> silent_discover_receive(int udp_socket, uint64_t expected_message_seq) {
        multiset<ServerData, std::greater<>> servers;
        struct ComplexMessage message_received{};
        struct sockaddr_in source_address{};
        socklen_t addr_length = sizeof(struct sockaddr_in);
        ssize_t recv_len;
        bool timeout_occ = false;

        set_socket_timeout(udp_socket);
        in_port_t source_port;
        string source_ip;
        auto wait_start_time = std::chrono::high_resolution_clock::now();
        while (!timeout_occ) {
            auto curr_time = std::chrono::high_resolution_clock::now();
            std::chrono::duration<double, std::milli> elapsed_time = curr_time - wait_start_time;
            if (elapsed_time.count() / 1000 >= timeout) {
                timeout_occ = true;
            } else {
                recv_len = recvfrom(udp_socket, &message_received, sizeof(struct ComplexMessage), 0, (struct sockaddr*)&source_address, &addr_length);
                if (recv_len > 0) {
                    source_ip = inet_ntoa(source_address.sin_addr);
                    source_port = be16toh(source_address.sin_port);
                    if (is_expected_command(message_received.command, cp::discover_response) && is_valid_message(message_received, expected_message_seq)) {
                        servers.insert(ServerData(inet_ntoa(source_address.sin_addr),
                                                  be64toh(message_received.param))); // TODO jakaś fuinkcja parsująca i rzucająca wyjątek, try catch i handle it
                    } else {
                        cerr << format("[PCKG ERROR] Skipping invalid package from %1%:%2%.") %source_ip %source_port;
                        cerr << "[PCKG ERROR] Skipping invalid package from " << source_ip << ":" << source_port <<"." << endl;
                        BOOST_LOG_TRIVIAL(info) << "[PCKG ERROR] Skipping invalid package from " << source_ip << ":" << source_port <<".";
                    }
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

    void display_files_list(char data[], uint32_t list_length, const char* server_ip) {
//        int i = 0;
//        while (i < list_length && data[i] != '\0') {
//            for (; data[i] != '\n'; ++i)
//                cout << data[i];
//            cout << " (" << (server_ip) << ")" << ", i = " << i << endl;
//            ++i;
//        }

//        string filename;
//        int next_filename_start = 0, next_filename_end, len;
//        while (next_filename_start < list_length) {
//            next_filename_end = data.find('\n', next_filename_start);
//            len = next_filename_end - next_filename_start;
//            filename = data.substr(next_filename_start, len);
//            cout << format("%1% (%2%)") %filename %server_ip << endl;
//            next_filename_start = next_filename_end + 1;
//        }

//        data[list_length - 1] = '\0';
        char *filename = strtok(data, "\n");
        while (filename != nullptr) {
            cout << filename << " (" << server_ip << ")" << endl;
            this->last_search_results[filename] = server_ip;
            filename = strtok(nullptr, "\n");
        }
    }

    // Memorizes only last occurrence of given filename
    void update_search_result(string_view data, uint32_t list_length, const char* server_ip) {
//        data[list_length - 1] = '\0';
//        char *filename= strtok(data, "\n");
//        while (filename != nullptr) {
//            cout << filename << endl;
//            this->last_search_results[filename] = server_ip;
//            filename = strtok(nullptr, "\n");
//        }
        string filename;
        int next_filename_start = 0, next_filename_end, len;
        while (next_filename_start < list_length) {
            next_filename_end = data.find('\n', next_filename_start);
            len = next_filename_end - next_filename_start;
            filename = data.substr(next_filename_start, len);
            this->last_search_results[filename] = server_ip;
            next_filename_start = next_filename_end + 1;
        }
    }

    void display_list_of_known_files() {
        cout << "List of known files:" << endl;
        for (auto const& [filename, ip_source]: this->last_search_results)
            cout << filename << " from " << ip_source << endl;
    }

    void send_get_files_list_message(int udp_socket, const string &pattern) {
        SimpleMessage message{htobe64(generate_message_sequence()), cp::files_list_request, pattern.c_str()};
        send_message_multicast_udp(udp_socket, message, pattern.length());
    }

    void receive_search_respond(int udp_socket) {
        struct SimpleMessage message{};
        struct sockaddr_in source_address{};
        socklen_t addr_length = sizeof(struct sockaddr_in);
        ssize_t recv_len;
        bool timeout_occ = false;

        set_socket_timeout(udp_socket);

        in_port_t source_port;
        string source_ip;
        auto wait_start_time = std::chrono::high_resolution_clock::now();
        while (!timeout_occ) {
            auto curr_time = std::chrono::high_resolution_clock::now();
            std::chrono::duration<double, std::milli> elapsed_time = curr_time - wait_start_time;
            if (elapsed_time.count() / 1000 >= timeout) {
                timeout_occ = true;
            } else {
                recv_len = recvfrom(udp_socket, &message, sizeof(message), 0, (struct sockaddr*)&source_address, &addr_length);
                if (recv_len > 0) { /// TODO
                    source_ip = inet_ntoa(source_address.sin_addr);
                    source_port = be16toh(source_address.sin_port);
                    if (is_valid_data(message.data, recv_len - const_variables::simple_message_no_data_size)) {
                        display_files_list(message.data, recv_len - const_variables::simple_message_no_data_size, source_ip.c_str());
                       //update_search_result(message.data, recv_len - const_variables::simple_message_no_data_size, source_ip.c_str());
                    } else {
                        cerr << format("[PCKG ERROR] Skipping invalid package from %1%:%2%.") %source_ip %source_port;
                        cerr << "[PCKG ERROR] Skipping invalid package from " << source_ip << ":" << source_port <<"." << endl;
                        BOOST_LOG_TRIVIAL(info) << "[PCKG ERROR] Skipping invalid package from " << source_ip << ":" << source_port <<".";
                    }
                }
            }
        }
    }

    void search(const string& pattern) {
        BOOST_LOG_TRIVIAL(info) << format("Starting search, pattern=%1%...") % pattern;
        int udp_socket = create_multicast_udp_socket();  // todo close socket
        send_get_files_list_message(udp_socket, pattern);
        receive_search_respond(udp_socket);
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
    static in_port_t receive_fetch_file_response(int udp_socket, uint64_t expected_message_sequence) {
        struct ComplexMessage message{};
        struct sockaddr_in source_address{};
        socklen_t addr_length = sizeof(struct sockaddr_in);
        ssize_t recv_len;

        recv_len = recvfrom(udp_socket, &message, sizeof(message), 0, (struct sockaddr*)&source_address, &addr_length);
        if (recv_len > 0) {
            string source_ip = inet_ntoa(source_address.sin_addr);
            uint16_t source_port = be16toh(source_address.sin_port);
            if (is_valid_message(message, expected_message_sequence) && is_expected_command(message.command, cp::file_get_response)) {
                BOOST_LOG_TRIVIAL(info) << format("Get file response received: port=%1% filename=%2%, source=%3%:%4%")
                                           %be64toh(message.param) %message.data %source_ip %source_port;
            } else {
                cerr << "[PCKG ERROR] Skipping invalid package from " << source_ip << ":" << source_port <<"." << endl;
            }
        }

        return be64toh(message.param);
    }

    void fetch_file_via_tcp(const char* server_ip, in_port_t server_port, const string& filename) {
        BOOST_LOG_TRIVIAL(trace) << "Starting downloading file via tcp...";
        int tcp_socket = create_and_connect_tcp_socket(server_ip, server_port);
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



    // TODO nie działa at all
    void fetch(const string& filename) {
        BOOST_LOG_TRIVIAL(trace) << format("Starting fetch, filename=%1%...") % filename;
        if (this->last_search_results.find(filename) == this->last_search_results.end()) {
            cout << "Unknown file" << endl;
        } else {
            // TODO
            /**
             * Workflow:
             * 1. UDP simple_cmd "GET" + nazwa pliku w data
             * 2. Odbierz od servera cmplx_cmd, cmt == CONNECTME, param = port tcp na którym server czeka, data = nazwa pliku który zostanie wysłany
             * 3. Nawiąrz połączenie TCP z serverem
             * 4. Pobieraj plik w dowhile i go zapisuje
             */

            string server_ip = this->last_search_results[filename];
            int unicast_socket = create_unicast_udp_socket();
            uint64_t expected_message_seq = send_get_file_message(unicast_socket, server_ip.c_str(), filename);
            in_port_t tcp_port = receive_fetch_file_response(unicast_socket, expected_message_seq);
            fetch_file_via_tcp(server_ip.c_str(), tcp_port, filename);
        }
        BOOST_LOG_TRIVIAL(trace) << "Ending fetch";
    }

    /*************************************************** ADD FILE *****************************************************/

    static bool can_upload_file(ComplexMessage server_response) {
        return server_response.command == cp::file_add_acceptance;
    }

    /// return sent message's message_sequence
    uint64_t send_upload_file_request(int udp_socket, const char *destination_ip, const string &filename) {
        uint64_t message_sequence = generate_message_sequence();
        ComplexMessage message{htobe64(message_sequence), cp::file_add_request, filename.c_str(), htobe64(get_file_size(this->out_folder + filename))};
        send_message_unicast_udp(udp_socket, destination_ip, message, filename.length());
        return message_sequence;
    }

    static ComplexMessage send_udp_message(int udp_socket, ComplexMessage message) {

    }

    static ComplexMessage receive_udp_message(int upd_socket, struct sockaddr_in source_address) {

    }

    // TODO wredny server fałszyyw moze wyslac wszystko dobrze ale nie być tym serverem...
    static ComplexMessage receive_upload_file_response(int udp_socket, uint64_t expected_message_sequence) {
        struct ComplexMessage message{};
        struct sockaddr_in source_address{};
        socklen_t addr_length = sizeof(struct sockaddr_in);
        ssize_t recv_len;

        recv_len = recvfrom(udp_socket, &message, sizeof(message), 0, (struct sockaddr*)&source_address, &addr_length);
        if (recv_len > 0) {
            string source_ip = inet_ntoa(source_address.sin_addr);
            uint16_t source_port = be16toh(source_address.sin_port);
            if (is_valid_message(message, expected_message_sequence) && is_expected_command(message.command, cp::file_add_acceptance)) {
                BOOST_LOG_TRIVIAL(info) << format("Get file response received: port=%1% filename=%2%, source=%3%:%4%")
                                           %be64toh(message.param) %message.data %source_ip %source_port;
            } else {
                cerr << "[PCKG ERROR] Skipping invalid package from " << source_ip << ":" << source_port <<"." << endl;
            }
        }

        return message;
    }

    /**
     *
     * @param server_ip
     * @param filename
     * @return If acceptect tcp_port on which server expects connection in order to upload file
     *         -1 otherwise
     */
    ComplexMessage ask_server_to_upload_file(const char* server_ip, const char* filename) {
        int udp_socket = create_multicast_udp_socket();
        uint64_t message_sequence = send_upload_file_request(udp_socket, server_ip, filename);
        return receive_upload_file_response(udp_socket, message_sequence);
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

    void upload_file_via_tcp(const char* server_ip, in_port_t server_port, const char* filename) {
        BOOST_LOG_TRIVIAL(trace) << "Starting uploading file via tcp...";
        int tcp_socket = create_and_connect_tcp_socket(server_ip, server_port);

        fs::path file_path{this->out_folder + filename};
        std::ifstream file_stream{file_path.c_str(), std::ios::binary};

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
        cout << "File " << filename << " uploaded (" << server_ip << ":" << server_port << ")" << endl;
    }

    bool at_least_on_server_has_space(multiset<ServerData, std::greater<>>& servers, size_t space_required) {
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
    void upload(const string& filename) {
        BOOST_LOG_TRIVIAL(trace) << "Starting upload procedure...";
        ComplexMessage server_response{};

        multiset<ServerData, std::greater<>> servers = silent_discover();
        if (at_least_on_server_has_space(servers, 0)) {
            for (const auto &server : servers) {
                // TODO walidacja wiadomosci tu czy wyzej? hm hm
                server_response = ask_server_to_upload_file(server.ip_addr, filename.c_str());

                if (can_upload_file(server_response)) {
                    BOOST_LOG_TRIVIAL(trace) << "Creating thread";
                    // TODO: variable: "running", thread checks wheter it's true, if not then it should terminate
                    std::thread thread {&Client::upload_file_via_tcp, this, server.ip_addr,
                                       htobe64(server_response.param), filename.c_str()};
                    thread.detach();
                    BOOST_LOG_TRIVIAL(trace) << "Thread detached";
                    break;
                }
            }
        } else {
            cout << format("File %1% too big") %filename << endl;
        }

        BOOST_LOG_TRIVIAL(trace) << "Ending upload procedure...";
    }

    /************************************************* REMOVE FILE ****************************************************/
    void remove(const string& filename) {
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

    }

public:
    Client(const char* mcast_addr, uint16_t cmd_port, string folder, const uint16_t timeout)
            : mcast_addr(mcast_addr),
              cmd_port(cmd_port),
              out_folder(std::move(folder)),
              timeout(timeout),
              generator(std::random_device{}())
    {}

    Client(ClientSettings& settings)
            : Client(settings.mcast_addr.c_str(), settings.cmd_port, settings.out_folder, settings.timeout)
    {}


    static vector<string> read_user_command() {
        vector<string> tokenized_command;
        string input;
        getline(cin, input);
        boost::split(tokenized_command, input, [](char c) {
            return iswspace(c);
        },
        boost::token_compress_on);

        if (tokenized_command.size() > 2)

    }



    void run() {
        string comm, param;
        bool exit = false;
        vector<string>

        while (!exit) {
            display_log_separator();
            cout << "Enter command: " << endl;

            cin >> comm;
            boost::algorithm::to_lower(comm);
            if (comm == "exit") {
                cout << comm;
                exit = true;
            } else if (comm == "discover") {
                discover();
            } else {
                if (comm == "search") {
                    cin >> param;
                    boost::algorithm::to_lower(param);
                    search(param);
                } else if (comm == "fetch") {
                    cin >> param;
                    boost::algorithm::to_lower(param);
                    fetch(param);
                } else if (comm == "upload") {
                    cin >> param;
                    boost::algorithm::to_lower(param);
                    upload(param);
                } else if (comm == "remove") {
                    cin >> param;
                    boost::algorithm::to_lower(param);
                    remove(param);
                } else {
                    cout << "Unknown command\n";
                }
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

