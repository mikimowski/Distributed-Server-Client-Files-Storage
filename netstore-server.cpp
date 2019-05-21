#include <iostream>
#include <boost/filesystem.hpp>
#include <boost/program_options.hpp>
#include <boost/algorithm/string.hpp>
#include <vector>

#include <unistd.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/types.h>
#include <sys/socket.h>

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

namespace fs = boost::filesystem;
namespace po = boost::program_options;
namespace cp = communication_protocol;

struct server_parameters {
    string mcast_addr;
    in_port_t cmd_port;
    uint64_t max_space;
    string shared_folder;
    uint16_t timeout;

    server_parameters() = default;

    void display() {
        cout << "Server settings:" << endl;
        cout << "MCAST_ADDR = " << this->mcast_addr << endl;
        cout << "CMD_PORT = " << this->cmd_port << endl;
        cout << "MAX_SPACE = " << this->max_space << endl;
        cout << "SHARED_FOLDER = " << this->shared_folder << endl;
        cout << "TIMEOUT = " << this->timeout << endl;
    }
};

struct server_parameters parse_program_arguments(int argc, const char *argv[]) {
    struct server_parameters server_parameters;

    po::options_description description {"Program options"};
    description.add_options()
            ("help,h", "Help screen")
            ("MCAST_ADDR,g", po::value<string>(&server_parameters.mcast_addr)->required(), "Multicast address")
            ("CMD_PORT,p", po::value<in_port_t>(&server_parameters.cmd_port)->required(), "UDP port used for sending and receiving commands")
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
    const char* mcast_addr;
    in_port_t cmd_port;
    uint64_t available_space;
    const string shared_folder;
    const uint16_t timeout;

    vector<fs::path> files_in_storage;

    /*** Receiving ***/
    int recv_socket;
    int mcast_socket;
    struct ip_mreq ip_mreq;

    /*** Sending ***/
    int send_socket;


    // TODO: Ask whether directories should be listed
    void generate_files_in_storage() {
        for (const auto& it: fs::directory_iterator(this->shared_folder)) {
          if (fs::is_regular_file(it)) {
                files_in_storage.emplace_back(it.path());
                this->available_space -= fs::file_size(it.path());
           }
        }
    }

public:
    Server(const char* mcast_addr, in_port_t cmd_port, uint64_t available_space, string shared_folder_path, uint16_t timeout)
    : mcast_addr(mcast_addr),
    cmd_port(cmd_port),
    available_space(available_space),
    shared_folder(move(shared_folder_path)),
    timeout(timeout)
    {
        generate_files_in_storage();
    }

    Server(const struct server_parameters& server_parameters)
            : mcast_addr(server_parameters.mcast_addr.c_str()),
              cmd_port(server_parameters.cmd_port),
              available_space(server_parameters.max_space),
              shared_folder(server_parameters.shared_folder),
              timeout(server_parameters.timeout)
    {
        generate_files_in_storage();
    }

    void init() {
        if ((recv_socket = socket(AF_INET, SOCK_DGRAM, 0)) < 0)
            syserr("socket");
        join_multicast_group(mcast_addr);
//        int reuse = 1;
//        if (setsockopt(recv_socket, SOL_SOCKET, SO_REUSEADDR, (const char*)&reuse, sizeof(reuse)) < 0)
//            perror("setsockopt(SO_REUSEADDR) failed");
    }

    void join_multicast_group(const char* mcast_recv_dotted_addr) {
        ip_mreq.imr_interface.s_addr = htonl(INADDR_ANY);
        if (inet_aton(mcast_recv_dotted_addr, &ip_mreq.imr_multiaddr) == 0)
            syserr("inet_aton");
        if (setsockopt(recv_socket, IPPROTO_IP, IP_ADD_MEMBERSHIP, (void*)&ip_mreq, sizeof ip_mreq) < 0)
            syserr("setsockopt");
    }

    void leave_multicast_group() {
        if (setsockopt(recv_socket, IPPROTO_IP, IP_DROP_MEMBERSHIP, (void*)&ip_mreq, sizeof ip_mreq) < 0)
            syserr("setsockopt");
    }


    void fill_discover_response_cmd(struct complex_cmd& cmd) {
        memset(&cmd, 0, sizeof(struct complex_cmd));
        set_cmd(cmd, cp::discover_response);
        cmd.param = available_space;
        strcpy(cmd.data, mcast_addr);
    }

    void discover_response(struct sockaddr_in& recv_addr) {
        int sock, optval;
        struct complex_cmd msg_send;
        struct sockaddr_in send_addr;

        if ((sock = socket(AF_INET, SOCK_DGRAM, 0)) < 0)
            syserr("socket");

        in_port_t remote_port = cmd_port;
        send_addr.sin_family = AF_INET;
        send_addr.sin_port = htons(remote_port);
        if (inet_aton(mcast_addr, &send_addr.sin_addr) == 0)
            syserr("inet_aton");

        fill_discover_response_cmd(msg_send);
        if (sendto(sock, &msg_send, sizeof(struct complex_cmd), 0, (struct sockaddr*) &recv_addr, sizeof(recv_addr)) < 0)
            syserr("sendto");

        close(sock);
    }

    void discover_receive() {
        struct simple_cmd msg_recv;
        ssize_t recv_len;
        struct sockaddr_in src_addr;
        socklen_t addrlen = sizeof(struct sockaddr_in);

        struct sockaddr_in local_address;
        in_port_t local_port = cmd_port;
        local_address.sin_family = AF_INET;
        local_address.sin_addr.s_addr = htonl(INADDR_ANY);
        local_address.sin_port = htons(local_port);
        if (bind(recv_socket, (struct sockaddr*) &local_address, sizeof(local_address)) < 0)
            syserr("bind");

        cout << "hoping to receive something" << endl;
        recv_len = recvfrom(recv_socket, &msg_recv, sizeof(msg_recv), 0, (struct sockaddr*) &src_addr, &addrlen); // UDP ZAWSZE CAŁY! CZYLI JEDEN READ!
        if (recv_len < 0) {
            syserr("read");
        } else {
            printf("Received from: %s\n", inet_ntoa(src_addr.sin_addr));
            printf("read %zd bytes\n", recv_len);
        }

        cout << "received something" << endl;
        discover_response(src_addr);
    }

    void run() {
        discover_receive();
    }

};


int main(int argc, const char *argv[]) {
    struct server_parameters server_parameters = parse_program_arguments(argc, argv);
    server_parameters.display();
    Server server {server_parameters};
    server.init();
    server.run();

    return 0;
}
