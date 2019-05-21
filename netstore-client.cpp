#include <iostream>
#include <boost/program_options.hpp>
#include <boost/algorithm/string.hpp>

#include <unistd.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/types.h>
#include <sys/socket.h>

#include "err.h"
#include "helper.h"

constexpr uint16_t default_timeout = 5;
constexpr uint16_t max_timeout = 300;

using namespace std;
namespace po = boost::program_options;
namespace cp = communication_protocol;

struct server_data {
    uint64_t available_space;

};

struct client_settings {
    string mcast_addr;
    string cmd_port;
    string folder;
    uint16_t timeout;
};

struct client_settings parse_program_arguments(int argc, const char *argv[]) {
    struct client_settings client_settings{};

    po::options_description description {"Program options"};
    description.add_options()
        ("help,h", "Help screen")
        ("MCAST_ADDR,g", po::value<string>(&client_settings.mcast_addr)->required(), "Multicast address")
        ("CMD_PORT,p", po::value<string>(&client_settings.cmd_port)->required(), "UDP port on which servers are listening")
        ("OUT_FLDR,o", po::value<string>(&client_settings.folder)->required(), "Path to the directory in which files should be saved")
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

void get_user_command() {
    string comm, param;

    cin >> comm;
    boost::algorithm::to_lower(comm);
    if (comm == "exit") {
        cout << comm;
    } else if (comm == "discover") {

    } else {
        cin >> param;
        boost::algorithm::to_lower(param);
        if (comm == "fetch") {
        } else if (comm == "upload") {
        } else if (comm == "remove") {
        } else {
            cout << "Unknown command\n";
        }
    }
}



class Client {
    const char* mcast_addr;
    const char* cmd_port;
    const string folder;
    const uint16_t timeout;

    uint64_t next_cmd_seq;


    void init() {

    }

    void display_server_discovered_info(const char* server_ip, const char* server_mcast_addr, uint64_t server_space) {
        cout << "Found " << server_ip << "(" << server_mcast_addr << ") with free space " << server_space << endl;
    }

    bool correct_cmd_seq(uint64_t expected, uint64_t received) {
        return expected == received;
    }

    bool valid_discover_response(struct complex_cmd& msg) {
    }

    void discover() {
        int sock, optval;

        if ((sock = socket(AF_INET, SOCK_DGRAM, 0)) < 0)
            syserr("socket");

        /* uaktywnienie rozgłaszania (ang. broadcast) */
        optval = 1;
        if (setsockopt(sock, SOL_SOCKET, SO_BROADCAST, (void*)&optval, sizeof optval) < 0)
            syserr("setsockopt broadcast");

        /* ustawienie TTL dla datagramów rozsyłanych do grupy */
        optval = timeout;
        if (setsockopt(sock, IPPROTO_IP, IP_MULTICAST_TTL, (void*)&optval, sizeof optval) < 0)
            syserr("setsockopt multicast ttl");

        // send on multicast
        /* ustawienie adresu i portu odbiorcy */
        struct simple_cmd msg_send;
        struct sockaddr_in send_addr;
        in_port_t send_port = (in_port_t)stoi(cmd_port);
        set_cmd(msg_send, cp::discover_request);

        send_addr.sin_family = AF_INET;
        send_addr.sin_port = htons(send_port);
        if (inet_aton(mcast_addr, &send_addr.sin_addr) == 0)
            syserr("inet_aton");
        if (sendto(sock, &msg_send, sizeof(msg_send), 0, (struct sockaddr*) &send_addr, sizeof(send_addr)) != sizeof(msg_send))
            syserr("sendto");

        // receive
        struct complex_cmd msg_recv;
        struct sockaddr_in src_addr;
        socklen_t addrlen = sizeof(struct sockaddr_in);
        ssize_t recv_len;
        char buffer[65507];// 2^16 = IP packet has 16 bits, UDP provides datagram as a part of IP packet

        for (int i = 0; i < 2; ++i) { //  Roboczo dwa servery będą
            recv_len = recvfrom(sock, &msg_recv, sizeof(struct complex_cmd), 0, (struct sockaddr*)&src_addr, &addrlen); // UDP ZAWSZE CAŁY! CZYLI JEDEN READ!
            if (recv_len < 0) {
                syserr("read");
            } else {
                printf("Received from: %s\n", inet_ntoa(src_addr.sin_addr));
                display_server_discovered_info(inet_ntoa(src_addr.sin_addr), msg_recv.data, msg_recv.param);
            }
        }
    }

public:
    Client(const char* mcast_addr, const char* cmd_port, const string& folder, const uint16_t timeout)
            : next_cmd_seq(0),
              mcast_addr(mcast_addr),
              cmd_port(cmd_port),
              folder(folder),
              timeout(timeout)
    {}

    Client(struct client_settings& settings)
            : Client(settings.mcast_addr.c_str(), settings.cmd_port.c_str(), settings.folder, settings.timeout)
    {}

    void run() {
        discover();
    }

    friend ostream& operator << (ostream &out, const Client &client) {
        out << "Client:" << endl;
        out << "MCAST_ADDR = " << client.mcast_addr << endl;
        out << "CMD_PORT = " << client.cmd_port << endl;
        out << "FOLDER = " << client.folder << endl;
        out << "TIMEOUT = " << client.timeout << endl;
        out << "NEXT_CMD_SEQ = " << client.next_cmd_seq << endl;

        return out;
    }
};


int main(int argc, const char *argv[]) {
    struct client_settings client_settings = parse_program_arguments(argc, argv);
   // get_user_command();
    Client client {client_settings};
    cout << client;
    client.run();

    return 0;
}

