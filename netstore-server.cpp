#include <utility>

#include <iostream>
#include <boost/filesystem.hpp>
#include <boost/program_options.hpp>
#include <boost/algorithm/string.hpp>
#include <vector>
#include <random>

#include <tuple>

#include <unistd.h>
#include <stdint.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>

#include "err.h"
#include "helper.h"

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
constexpr uint64_t default_max_disc_space = 52428800;

#define MAX_QUEUE_LENGTH 5

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
    const string multicast_address;
    in_port_t cmd_port;
    uint64_t used_space = 0;
    uint64_t max_available_space;
    const string shared_folder;
    const uint16_t timeout;

    vector<fs::path> files_in_storage;

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

        // Retrieve assigned port
        if (getsockname(tcp_socket, (struct sockaddr*) &local_addr, &addrlen) < 0)
            syserr("getsockname");
        in_port_t tcp_port = ntohs(local_addr.sin_port);

        cout << "TCP socket created, port chosen = " << tcp_port << endl;
        return {tcp_socket, tcp_port};
    }

public:
    Server(string mcast_addr, in_port_t cmd_port, uint64_t max_available_space, string shared_folder_path, uint16_t timeout)
        : multicast_address(std::move(mcast_addr)),
        cmd_port(cmd_port),
        max_available_space(max_available_space),
        shared_folder(move(shared_folder_path)),
        timeout(timeout)
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
        if (inet_aton(this->multicast_address.c_str(), &ip_mreq.imr_multiaddr) == 0)
            syserr("inet_aton");
        if (setsockopt(recv_socket, IPPROTO_IP, IP_ADD_MEMBERSHIP, (void*)&ip_mreq, sizeof ip_mreq) < 0)
            syserr("setsockopt");
    }

    void leave_multicast_group() {
        if (setsockopt(recv_socket, IPPROTO_IP, IP_DROP_MEMBERSHIP, (void*)&ip_mreq, sizeof ip_mreq) < 0)
            syserr("setsockopt");
    }

    bool is_enough_space(uint64_t file_size) {
        return file_size <= this->get_available_space();
    }

    void send_message_udp(const SimpleMessage &message, struct sockaddr_in &destination_address, uint16_t data_length = 0) {
        uint16_t message_length = const_variables::simple_message_no_data_size + data_length;
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
        uint16_t message_length = const_variables::complex_message_no_data_size + data_length;
        int sock;
        if ((sock = socket(AF_INET, SOCK_DGRAM, 0)) < 0)
            syserr("socket");
        if (sendto(sock, &message, message_length, 0, (struct sockaddr*) &destination_address, sizeof(destination_address)) != message_length)
            syserr("sendto");
        if (close(sock) < 0)
            syserr("sock");
        cerr << "UDP command sent" << endl;
    }

    void handle_discover_request(struct sockaddr_in &destination_address, uint64_t message_seq) {
        struct ComplexMessage message{htobe64(message_seq), cp::discover_response, this->multicast_address.c_str(), htobe64(this->get_available_space())};
        cerr << "Sending to: " << inet_ntoa(destination_address.sin_addr) << ":" << ntohs(destination_address.sin_port) << endl;
        cerr << message << endl;
        send_message_udp(message, destination_address, const_variables::max_command_length + 2 * sizeof(uint64_t) + this->multicast_address.length());
    }

    static void fill_message_with_filename(struct SimpleMessage& msg_send, uint64_t *start_index, const string& filename) {
        strcpy(msg_send.data + *start_index, filename.c_str());
        *start_index += filename.length();
        msg_send.data[(*start_index)++] = '\n';
    }

    void handle_files_list_request(struct sockaddr_in &recv_addr, uint64_t message_seq, const char *pattern) {
        cout << "Files list request for pattern: " << pattern << endl;
        int sock;
        struct SimpleMessage message{htobe64(message_seq), cp::files_list_response};
        struct sockaddr_in send_addr{};

        if ((sock = socket(AF_INET, SOCK_DGRAM, 0)) < 0)
            syserr("socket");

        in_port_t remote_port = cmd_port;
        send_addr.sin_family = AF_INET;
        send_addr.sin_port = htons(remote_port);
        if (inet_aton(multicast_address.c_str(), &send_addr.sin_addr) == 0) // TTODO jak to mcast... chyba zwykły jakis
            syserr("inet_aton");

        uint64_t curr_data_len = 0;
        string_view filename;
        for (const fs::path& file : this->files_in_storage) {
            filename = file.filename().generic_string();
            if (is_substring(pattern, filename)) {
                if (const_variables::max_simple_data_size > curr_data_len + filename.length()) {
                    fill_message_with_filename(message, &curr_data_len, file.filename().generic_string());
                } else {
                    send_message_udp(message, recv_addr, curr_data_len);
                    curr_data_len = 0;
                    memset(&message.data, '\0', sizeof(message.data));
                }
            }
        }
        if (curr_data_len > 0)
            send_message_udp(message, recv_addr, curr_data_len);

        close(sock);
        cout << "All files matching given pattern has been sent" << endl;
    }

    vector<fs::path>::iterator find_file(const char* filename) {
        for (auto it = this->files_in_storage.begin(); it != this->files_in_storage.end(); it++)
            if (it->filename() == filename)
                return it;

        return this->files_in_storage.end();
    }

    /**
     * 1. Select free port
     * 2. Send chosen port to the client
     * 3. Wait on this port for TCP connection with the client
     */
    void handle_file_request(struct sockaddr_in &destination_address, uint64_t message_seq, const char *filename) {
        cout << "File request, filename = " << filename << endl;

        if (find_file(filename) != this->files_in_storage.end()) {
            auto [tcp_socket, tcp_port] = create_tcp_socket();

            ComplexMessage message{htobe64(message_seq), cp::file_get_response, filename, tcp_port};
            send_message_udp(message, destination_address, strlen(filename));
            cerr << "TCP port info sent" << endl;
        } else {
            cout << "Incorrect file request received" << endl;
        }
    }

    void handle_upload_request(struct sockaddr_in &destination_address, uint64_t message_seq,
            const char *filename, uint64_t file_size) {
        cout << "File upload request, filename = " << filename << ", filesize = " << file_size << endl;
        if (is_enough_space(file_size)) {
            cout << "Accepting file upload request" << endl;
            auto [tcp_socket, tcp_port] = create_tcp_socket();

            // Send info about tcp port
            ComplexMessage message{htobe64(message_seq), cp::file_add_acceptance, "", htobe64(tcp_port)};
            send_message_udp(message, destination_address);
            upload_file_via_tcp(tcp_socket, filename);
        } else {
            cerr << "Rejecting file upload request" << endl;
            SimpleMessage message{htobe64(message_seq), cp::file_add_refusal};
            send_message_udp(message, destination_address);
        }
    }

    void upload_file_via_tcp(int tcp_socket, const char* filename) {
        cout << "Uploading file via tcp..." << endl;

        struct sockaddr_in source_address{};
        memset(&source_address, 0, sizeof(source_address));
        socklen_t len = sizeof(source_address);
        int sock = accept(tcp_socket, (struct sockaddr*) &source_address, &len);

        fs::path file_path(this->shared_folder + filename);
        fs::ofstream ofs(file_path,std::ofstream::binary);

        char buffer[MAX_BUFFER_SIZE];
        while ((len = read(sock, buffer, sizeof(buffer))) > 0) {
            ofs.write(buffer, len);
        }
        ofs.close();

        if (close(sock) < 0)
            syserr("close");

        cout << "Ending uploading file via tcp" << endl;
    }

    void handle_remove_request(struct sockaddr_in &destination_address, uint64_t message_seq, const char *filename) {

    }

    /// Rzuca wyjątek jeśli incorrect message command
    static const char* get_message_command(const ComplexMessage& message) {
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
            display_log_separator();
            auto [received_message, source_address] = receive_next_message();
            string_view message_command = get_message_command(received_message);
            if (message_command == cp::discover_request) {
                auto *message = (SimpleMessage*) &received_message;
                handle_discover_request(source_address, be64toh(message->message_seq));
            } else if (message_command == cp::files_list_request) {
                auto *message = (SimpleMessage*) &received_message;
                cout << *message << endl;
                handle_files_list_request(source_address, be64toh(message->message_seq), message->data);
            } else if (message_command == cp::file_get_request) {
                auto *message = (SimpleMessage*) &received_message;
                cout << *message << endl;
                handle_file_request(source_address, be64toh(message->message_seq), message->data);
            } else if (message_command == cp::file_add_request) {
                handle_upload_request(source_address, be64toh(received_message.message_seq), received_message.data,
                                      received_message.param);
            } else if (message_command == cp::file_remove_request) {
                cout << received_message << endl;
                handle_remove_request(source_address, be64toh(received_message.message_seq), received_message.data);
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
