#include <utility>

#include <iostream>
#include <boost/filesystem.hpp>
#include <boost/program_options.hpp>
#include <boost/algorithm/string.hpp>
#include <vector>
#include <random>

#include <tuple>

#include <unistd.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>



#include "err.h"
#include "helper.h"


constexpr uint16_t default_timeout = 5;
constexpr uint16_t max_timeout = 300;
constexpr uint64_t default_max_disc_space = 52428800;

using std::string;
using std::cout;
using std::endl;
using std::cerr;
using std::to_string;
using std::vector;
using std::string_view;
using std::tuple;

namespace fs = boost::filesystem;
namespace po = boost::program_options;
namespace cp = communication_protocol;

struct ServerParameters {
    string mcast_addr;
    in_port_t cmd_port;
    uint64_t max_space;
    string shared_folder;
    uint16_t timeout;

    ServerParameters() = default;

    void display() {
        cout << "Server settings:" << endl;
        cout << "MCAST_ADDR = " << this->mcast_addr << endl;
        cout << "CMD_PORT = " << this->cmd_port << endl;
        cout << "MAX_SPACE = " << this->max_space << endl;
        cout << "SHARED_FOLDER = " << this->shared_folder << endl;
        cout << "TIMEOUT = " << this->timeout << endl;
    }
};

ServerParameters parse_program_arguments(int argc, const char *argv[]) {
    ServerParameters server_parameters;

    po::options_description description {"Program options"};
    description.add_options()
            ("help,h", "Help screen")
            ("MCAST_ADDR,g", po::value<string>(&server_parameters.mcast_addr)->required(), "Multicast address")
            ("CMD_PORT,p", po::value<in_port_t>(&server_parameters.cmd_port)->required(), "UDP port used for sending and receiving messages")
            ("MAX_SPACE,b", po::value<uint64_t>(&server_parameters.max_space)->default_value(default_max_disc_space),
                    ("Maximum disc space in this server node\n"
                     "  Default value: " + to_string(default_max_disc_space)).c_str())
            ("SHRD_FLDR,f", po::value<string>(&server_parameters.shared_folder)->required(), "Path to the directory in which files are stored")
            ("TIMEOUT,t", po::value<uint16_t>(&server_parameters.timeout)->default_value(default_timeout)->notifier([](uint16_t timeout) {
                 if (timeout > max_timeout) {
                     cerr << "TIMEOUT out of range\n";
                     exit(1);
                 }
             }),
             (("Maximum waiting time for connection from client\n"
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

    return server_parameters;
}

class Server {
    const string mcast_addr;
    in_port_t cmd_port;
    uint64_t used_space;
    uint64_t max_available_space;
    const string shared_folder;
    const uint16_t timeout;

    vector<fs::path> files_in_storage;
    std::mt19937_64 generator;
    std::uniform_int_distribution<uint64_t> uniform_distribution;

    /*** Receiving ***/
    int recv_socket;
    struct ip_mreq ip_mreq;

    /*** Sending ***/
    int send_socket;


    // TODO: Ask whether directories should be listed
    void generate_files_in_storage() {
        for (const auto& path: fs::directory_iterator(this->shared_folder)) {
          if (fs::is_regular_file(path)) {
                files_in_storage.emplace_back(path.path());
                this->used_space += fs::file_size(path.path());
           }
        }
    }

    uint64_t generate_message_sequence() {
        return uniform_distribution(generator);
    }

    uint64_t get_available_space() {
        return this->used_space > this->max_available_space ? 0 : this->max_available_space - this->used_space;
    }

    static tuple<int, in_port_t> create_tcp_socket() {
        int tcp_socket;
        struct sockaddr_in local_addr{};
        socklen_t addrlen = sizeof(local_addr);
        memset(&local_addr, 0, sizeof(local_addr)); // sin_port set to 0, therefore it will be set random free port
        local_addr.sin_family = AF_INET;
        local_addr.sin_addr.s_addr = htonl(INADDR_ANY);
        if ((tcp_socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)) < 0)
            syserr("socket");
        if (bind(tcp_socket, (struct sockaddr*) &local_addr, sizeof(local_addr)) < 0)
            syserr("bind");
        if (listen(tcp_socket, TCP_QUEUE_LENGTH) < 0)
            syserr("listen");

//        struct addrinfo addr_hints{};
//        struct addrinfo *addr_result{};
//
//        // Passing host/port string to struct addrinfo
//        memset(&addr_hints, 0, sizeof(struct addrinfo));
//        addr_hints.ai_family = AF_INET; // IPv4
//        addr_hints.ai_socktype = SOCK_STREAM;
//        addr_hints.ai_protocol = IPPROTO_TCP;
//        int error = getaddrinfo(client_ip, server_port, &addr_hints, &addr_result);
//        if (error == EAI_SYSTEM) { // system error
//            syserr("getaddrinfo: %s", gai_strerror(error));
//        } else if (error != 0) {
//            syserr("getaddrinfo: %s", gai_strerror(error));
//        }
//
//        // Initialize socket according to getaddrinfo results
//        if ((tcp_socket = socket(addr_result->ai_family, addr_result->ai_socktype, addr_result->ai_protocol)) < 0)
//            syserr("socket");
//
//        // Connect socket to the server
//        if (connect(tcp_socket, addr_result->ai_addr, addr_result->ai_addrlen) < 0)
//            syserr("connect");

        // Retrieve assigned port
        if (getsockname(tcp_socket, (struct sockaddr*) &local_addr, &addrlen) < 0)
            syserr("getsockname");
        in_port_t tcp_port = ntohs(local_addr.sin_port);

        cerr << "TCP socket created, port chosen = " << tcp_port << endl;
        return {tcp_socket, tcp_port};
    }

public:
    Server(string mcast_addr, in_port_t cmd_port, uint64_t max_available_space, string shared_folder_path, uint16_t timeout)
        : mcast_addr(std::move(mcast_addr)),
        cmd_port(cmd_port),
        max_available_space(max_available_space),
        shared_folder(move(shared_folder_path)),
        timeout(timeout),
        generator(std::random_device{}())
        {}

    Server(const struct ServerParameters& server_parameters)
        : Server(server_parameters.mcast_addr, server_parameters.cmd_port, server_parameters.max_space,
                server_parameters.shared_folder, server_parameters.timeout)
        {}

    void init_recv_socket() {
        if ((recv_socket = socket(AF_INET, SOCK_DGRAM, 0)) < 0)
            syserr("socket");
        join_multicast_group();

        int reuse = 1; // TODO wywalić domyślnie?
        if (setsockopt(recv_socket, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) < 0)
            syserr("setsockopt(SO_REUSEADDR) failed");

        struct sockaddr_in local_address{};
        local_address.sin_family = AF_INET;
        local_address.sin_addr.s_addr = htonl(INADDR_ANY);
        local_address.sin_port = htons(cmd_port);
        if (bind(recv_socket, (struct sockaddr*) &local_address, sizeof(local_address)) < 0)
            syserr("bind");
    }

    void init() {
        generate_files_in_storage();
        init_recv_socket();
    }

    void join_multicast_group() {
        ip_mreq.imr_interface.s_addr = htonl(INADDR_ANY);
        if (inet_aton(this->mcast_addr.c_str(), &ip_mreq.imr_multiaddr) == 0)
            syserr("inet_aton");
        if (setsockopt(recv_socket, IPPROTO_IP, IP_ADD_MEMBERSHIP, (void*)&ip_mreq, sizeof ip_mreq) < 0)
            syserr("setsockopt");
    }

    void leave_multicast_group() {
        if (setsockopt(recv_socket, IPPROTO_IP, IP_DROP_MEMBERSHIP, (void*)&ip_mreq, sizeof ip_mreq) < 0)
            syserr("setsockopt");
    }

    void send_message_udp(const SimpleMessage &message, struct sockaddr_in &destination_address, uint16_t data_length = 0) {
        uint16_t message_length = const_variables::simple_command_min_length + data_length;
        int sock;
        if ((sock = socket(AF_INET, SOCK_DGRAM, 0)) < 0)
            syserr("socket");
        if (sendto(sock, &message, message_length, 0, (struct sockaddr*) &destination_address, sizeof(destination_address)) != message_length)
            syserr("sendto");
        if (close(sock) < 0)
            syserr("sock");
        cerr << "UDP command sent" << endl;
    }

    void send_message_udp(const ComplexMessage &message, struct sockaddr_in &destination_address, uint16_t data_length = 0) {
        uint16_t message_length = const_variables::complex_command_min_length + data_length;
        int sock;
        if ((sock = socket(AF_INET, SOCK_DGRAM, 0)) < 0)
            syserr("socket");
        if (sendto(sock, &message, message_length, 0, (struct sockaddr*) &destination_address, sizeof(destination_address)) != message_length)
            syserr("sendto");
        if (close(sock) < 0)
            syserr("sock");
        cerr << "UDP command sent" << endl;
    }

    void receive_message() {

    }

    void discover_respond(struct sockaddr_in& destination_address, uint64_t message_seq) {
        struct ComplexMessage message{message_seq, cp::discover_response, this->mcast_addr.c_str(), htonl(this->get_available_space())};
        cerr << "Sending to: " << inet_ntoa(destination_address.sin_addr) << ":" << ntohs(destination_address.sin_port) << endl;
        send_message_udp(message, destination_address, const_variables::max_command_len + 2 * sizeof(uint64_t) + this->mcast_addr.length());
    }

//    void discover_receive() {
//        struct SimpleMessage msg_recv{};
//        ssize_t recv_len;
//        struct sockaddr_in src_addr{};
//        socklen_t addrlen = sizeof(struct sockaddr_in);
//
//        recv_len = recvfrom(recv_socket, &msg_recv, sizeof(msg_recv), 0, (struct sockaddr*) &src_addr, &addrlen);
//        if (recv_len < 0)
//            syserr("read");
//        cerr << "discover command received from: " << inet_ntoa(src_addr.sin_addr) << endl;
//    }

    void files_list_request_receive() {
        struct SimpleMessage msg_recv{};
        ssize_t recv_len;
        struct sockaddr_in src_addr{};
        socklen_t addrlen = sizeof(struct sockaddr_in);

        recv_len = recvfrom(recv_socket, &msg_recv, sizeof(msg_recv), 0, (struct sockaddr*) &src_addr, &addrlen);
        if (recv_len < 0)
            syserr("read");

        files_list_request_respond(src_addr, string(msg_recv.data).c_str());
    }

//    static void send_udp_msg(int socket, struct SimpleMessage& msg_send, struct sockaddr_in& recv_addr) {
//        if (sendto(socket, &msg_send, sizeof(msg_send), 0, (struct sockaddr*) &recv_addr, sizeof(recv_addr)) != sizeof(msg_send))
//            syserr("sendto");
//        cout << "UDP package sent" << endl;
//    }

    static void fill_cmd_with_filename(struct SimpleMessage& msg_send, uint32_t *start_index, const string& filename) {
        strcpy(msg_send.data + *start_index, filename.c_str());
        *start_index += filename.length();
        msg_send.data[(*start_index)++] = '\n';
    }

    /**
     * @return True if given pattern is a substring of given string,
     *         false otherwise
     */
    static bool is_substring(char const* pattern, const string_view& str) {
        return str.find(pattern) != string::npos;
    }


    void files_list_request_respond(struct sockaddr_in& recv_addr, const char* pattern) {
        cerr << "Files list request for pattern: " << pattern << endl;
        int sock;
        struct SimpleMessage msg_send{};
        struct sockaddr_in send_addr{};

        if ((sock = socket(AF_INET, SOCK_DGRAM, 0)) < 0)
            syserr("socket");

        in_port_t remote_port = cmd_port;
        send_addr.sin_family = AF_INET;
        send_addr.sin_port = htons(remote_port);
        if (inet_aton(mcast_addr.c_str(), &send_addr.sin_addr) == 0) // TTODO jak to mcast... chyba zwykły jakis
            syserr("inet_aton");

        uint32_t curr_data_len = 0;
        string_view filename;
        for (const fs::path& file : this->files_in_storage) {
            filename = file.filename().generic_string();
            if (is_substring(pattern, filename)) {
                if (const_variables::max_data_size > curr_data_len + filename.length()) {
                    fill_cmd_with_filename(msg_send, &curr_data_len, file.filename().generic_string());
                } else {
                    send_message_udp(msg_send, recv_addr, curr_data_len);
                    curr_data_len = 0;
                    msg_send.init();
                }
            }
        }
        if (curr_data_len > 0)
            send_message_udp(msg_send, recv_addr, curr_data_len);

        close(sock);
        cerr << "All files matching given pattern has been sent" << endl;
    }

    vector<fs::path>::iterator find_file(const char* filename) {
        for (auto it = this->files_in_storage.begin(); it != this->files_in_storage.end(); it++)
            if (it->filename() == filename)
                return it;

        return this->files_in_storage.end();
    }

//    void get_file_request_receive() {
//        struct SimpleMessage msg_recv{};
//        ssize_t recv_len;
//        struct sockaddr_in src_addr{};
//        socklen_t addrlen = sizeof(struct sockaddr_in);
//
//        recv_len = recvfrom(recv_socket, &msg_recv, sizeof(msg_recv), 0, (struct sockaddr*) &src_addr, &addrlen);
//        if (recv_len < 0)
//            syserr("read");
//
//        get_file_request_respond(src_addr, msg_recv.data);
//    }

    /**
     * 1. Select free port
     * 2. Send chosen port to the client
     * 3. Wait on this port for TCP connection with the client
     */
    void get_file_request_respond(struct sockaddr_in& destination_address, uint64_t message_seq, const char* filename) {
        cout << "File request, filename = " << filename << endl;

        if (find_file(filename) != this->files_in_storage.end()) {
            auto [tcp_socket, tcp_port] = create_tcp_socket();

            ComplexMessage message{message_seq, cp::file_get_response, filename, tcp_port};
            send_message_udp(message, destination_address, strlen(filename));
            cerr << "TCP port info sent" << endl;
        } else {
            cout << "Incorrect file request received" << endl;
        }
    }

    void upload_file_request_receive() {
        cerr << "Upload file receive" << endl;
        struct ComplexMessage msg_recv{};
        ssize_t recv_len;
        struct sockaddr_in src_addr{};
        socklen_t addrlen = sizeof(struct sockaddr_in);

        recv_len = recvfrom(recv_socket, &msg_recv, sizeof(msg_recv), 0, (struct sockaddr*) &src_addr, &addrlen);
        if (recv_len < 0)
            syserr("read");

        upload_file_request_respond(src_addr, msg_recv.data, msg_recv.param);
        cerr << "Ending upload file receive" << endl;
    }

    bool is_enough_space(uint64_t file_size) {
        return file_size <= this->get_available_space();
    }

    void upload_file_request_respond(struct sockaddr_in& destination_address, const char *filename, uint64_t file_size) {
        cerr << "File upload request, filename = " << filename << ", filesize = " << file_size << endl;

        if (is_enough_space(file_size)) {
            cerr << "Enough space" << endl;

            auto [tcp_sock, tcp_port] = create_tcp_socket();
//            // Prepare tcp socket
//            int tcp_sock;
//            struct sockaddr_in local_addr{};
//            socklen_t addrlen = sizeof(local_addr);
//            memset(&local_addr, 0, sizeof(local_addr)); // sin_port set to 0, therefore it will be set random free port
//            local_addr.sin_family = AF_INET;
//
//            if ((tcp_sock = socket(AF_INET, SOCK_DGRAM, 0)) < 0)
//                syserr("socket");
//            if (bind(tcp_sock, (struct sockaddr*) &local_addr, sizeof(local_addr)) < 0)
//                syserr("bind");
//            if (getsockname(tcp_sock, (struct sockaddr*) &local_addr, &addrlen) < 0)
//                syserr("getsockname");
//
//            in_port_t tcp_port = local_addr.sin_port;

            // Send info about tcp port
            uint64_t tmp_message_seq;
            ComplexMessage message{tmp_message_seq, cp::file_add_acceptance, "", tcp_port};
            send_message_udp(message, destination_address);

            cerr << "TCP port info sent" << endl;
        } else {
            cerr << "Not enough space or incorrect file name" << endl;
            uint64_t tmp_message_seq;
            SimpleMessage message{tmp_message_seq, cp::file_add_refusal};
        }
    }

    /// Rzuca wyjątek jeśli incorrect message command
    const char* get_message_command(const ComplexMessage& message) {
        if (strcmp(message.command, cp::discover_request) == 0)
            return cp::discover_request;
        else if (strcmp(message.command, cp::files_list_request) == 0)
            return cp::files_list_request;
        else if (strcmp(message.command, cp::file_add_request) == 0)
            return cp::file_add_request;
        else if (strcmp(message.command, cp::file_get_request) == 0)
            return cp::file_get_request;
        else if (strcmp(message.command, cp::file_remove_request) == 0)
            return cp::file_remove_request;
        else
            throw ("Unknown command expection");
    }

    tuple<ComplexMessage, struct sockaddr_in> receive_next_message() {
        ComplexMessage message;
        ssize_t recv_len;
        struct sockaddr_in source_address{};
        socklen_t addrlen = sizeof(struct sockaddr_in);

        recv_len = recvfrom(recv_socket, &message, sizeof(message), 0, (struct sockaddr*) &source_address, &addrlen);
        if (recv_len < 0)
            syserr("read");

        return {message, source_address};
    }

    void run() {
        while (true) {
            auto[received_message, source_address] = receive_next_message();
            string_view message_command = get_message_command(received_message);
            if (message_command == cp::discover_request) {
                SimpleMessage message{received_message};
                cout << message << endl;
                discover_respond(source_address, message.message_seq);
            } else if (message_command == cp::file_get_request) {

            } else if (message_command == cp::file_add_request) {

            } else if (message_command == cp::files_list_request) {

            } else if (message_command == cp::file_remove_request) {

            }
        }
    }
};


int main(int argc, const char *argv[]) {
    struct ServerParameters server_parameters = parse_program_arguments(argc, argv);
    server_parameters.display();
    Server server {server_parameters};
    server.init();
    cout << "Starting server..." << endl;
    server.run();

    return 0;
}
