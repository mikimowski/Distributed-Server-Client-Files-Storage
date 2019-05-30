#include <iostream>
#include <chrono>
#include <random>
#include <unordered_map>
#include <fstream>

#include <boost/algorithm/string.hpp>
#include <boost/filesystem.hpp>
#include <boost/format.hpp>

#include <unistd.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/types.h>
#include <sys/socket.h>

#include "client.h"
#include "helper.h"
#include "err.h"

using std::string;
using std::multiset;
using std::cerr;
using std::cout;
using std::endl;
using boost::format;
using std::tuple;

namespace cp = communication_protocol;
namespace fs = boost::filesystem;
namespace logging = boost::log;

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




static int create_multicast_udp_socket() {
    int mcast_udp_socket, optval;

    if ((mcast_udp_socket = socket(AF_INET, SOCK_DGRAM, 0)) < 0)
        syserr("socket");

    /* uaktywnienie rozgłaszania (ang. broadcast) */
    optval = 1;
    if (setsockopt(mcast_udp_socket, SOL_SOCKET, SO_BROADCAST, (void*)&optval, sizeof optval) < 0)
        syserr("setsockopt broadcast");

    /* ustawienie TTL dla datagramów rozsyłanych do grupy */
    optval = 5;
    if (setsockopt(mcast_udp_socket, IPPROTO_IP, IP_MULTICAST_TTL, (void*)&optval, sizeof optval) < 0)
        syserr("setsockopt multicast ttl");

    return mcast_udp_socket;
}

static int create_unicast_udp_socket() {
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

// TODO jakoś lepiej...
static bool is_expected_command(const char* command, const string& expected_command) {
    int i = 0;
    for (; i < expected_command.length(); i++)
        if (command[i] != expected_command[i])
            return false;
    while (i < cp::max_command_length)
        if (command[i++] != '\0')
            return false;
    return true;
}

static void message_validation(const ComplexMessage& message, uint64_t expected_message_seq, const string& expected_command, ssize_t message_size, const string& expected_data = "") {
    if (message_size < cp::complex_message_no_data_size)
        throw invalid_message("message too small");
    if (!is_expected_command(message.command, expected_command))
        throw invalid_message("invalid command");
    if (be64toh(message.message_seq) != expected_message_seq)
        throw invalid_message("invalid message seq");
    if (!is_valid_data(message.data, message_size - cp::complex_message_no_data_size)) //  TODO is it right? check!
        throw invalid_message("invalid message data");
}

static void message_validation(const SimpleMessage& message, uint64_t expected_message_seq, const string& expected_command, ssize_t message_size, const string& expected_data = "") {
    if (message_size < cp::simple_message_no_data_size)
        throw invalid_message("message too small");
    if (!is_expected_command(message.command, expected_command))
        throw invalid_message("invalid command");
    if (be64toh(message.message_seq) != expected_message_seq)
        throw invalid_message("invalid message seq");
    if (!is_valid_data(message.data, message_size - cp::simple_message_no_data_size)) //  TODO is it right? check!
        throw invalid_message("invalid message data");
    if (!expected_data.empty() && expected_data != message.data)
        throw invalid_message("unexpected data received");
}

// TODO throw expection if failed... ?? no way of failure...
static tuple<string, in_port_t> unpack_sockaddr(const struct sockaddr_in& address) {
    return {inet_ntoa(address.sin_addr), be16toh(address.sin_port)};
}


uint64_t Client::generate_message_sequence() {
    return uniform_distribution(generator);
}

void Client::send_message_multicast_udp(int sock, const SimpleMessage &message, uint16_t data_length) {
    uint16_t message_length = cp::simple_message_no_data_size + data_length;
    struct sockaddr_in destination_address{};
    destination_address.sin_family = AF_INET;
    destination_address.sin_port = htons(cmd_port);
    if (inet_aton(mcast_addr.c_str(), &destination_address.sin_addr) == 0)
        syserr("inet_aton");
    if (sendto(sock, &message, message_length, 0, (struct sockaddr*) &destination_address, sizeof(destination_address)) != message_length)
        syserr("sendto");

    BOOST_LOG_TRIVIAL(info) << "multicast udp message sent";
}

void Client::send_message_multicast_udp(int sock, const ComplexMessage &message, uint16_t data_length) {
    uint16_t message_length = cp::complex_message_no_data_size + data_length;
    struct sockaddr_in destination_address{};
    destination_address.sin_family = AF_INET;
    destination_address.sin_port = htons(cmd_port);
    if (inet_aton(mcast_addr.c_str(), &destination_address.sin_addr) == 0)
        syserr("inet_aton");
    if (sendto(sock, &message, message_length, 0, (struct sockaddr*) &destination_address, sizeof(destination_address)) != message_length)
        syserr("sendto");

    BOOST_LOG_TRIVIAL(info) << "multicast udp message sent";
}

void Client::send_message_unicast_udp(int sock, const char* destination_ip, const ComplexMessage &message, uint16_t data_length) {
    uint16_t message_length = cp::complex_message_no_data_size + data_length;
    struct sockaddr_in destination_address{};
    destination_address.sin_family = AF_INET;
    destination_address.sin_port = htons(cmd_port);
    if (inet_aton(destination_ip, &destination_address.sin_addr) == 0)
        syserr("inet_aton");
    if (sendto(sock, &message, message_length, 0, (struct sockaddr*) &destination_address, sizeof(destination_address)) != message_length)
        syserr("sendto");

    BOOST_LOG_TRIVIAL(info) << "unicast udp message sent";
}

void Client::send_message_unicast_udp(int sock, const char* destination_ip, const SimpleMessage &message, uint16_t data_length) {
    uint16_t message_length = cp::simple_message_no_data_size + data_length;
    struct sockaddr_in destination_address{};
    destination_address.sin_family = AF_INET;
    destination_address.sin_port = htons(cmd_port);
    if (inet_aton(destination_ip, &destination_address.sin_addr) == 0)
        syserr("inet_aton");
    if (sendto(sock, &message, message_length, 0, (struct sockaddr*) &destination_address, sizeof(destination_address)) != message_length)
        syserr("sendto");

    BOOST_LOG_TRIVIAL(info) << "unicast udp message sent";
}



/***************************************************** DISCOVER *******************************************************/

static void display_server_discovered_info(const char* server_ip, const char* server_mcast_addr, uint64_t server_space) {
    cout << format("Found %1%(%2%) with free space %3%\n") %server_ip %server_mcast_addr %server_space;
}



/**
 * @param udp_socket
 * @return Expected respond message's message_seq
 */
uint64_t Client::send_discover_message(int udp_socket) {
    uint64_t message_seq = generate_message_sequence();
    SimpleMessage message {htobe64(message_seq), cp::discover_request};
    send_message_multicast_udp(udp_socket, message);
    return message_seq;
}

void Client::receive_discover_response(int udp_socket, uint64_t expected_message_seq) {
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

void Client::discover() {
    BOOST_LOG_TRIVIAL(trace) << "Starting discover...";
    int udp_socket = create_multicast_udp_socket();
    uint64_t expected_message_seq = send_discover_message(udp_socket);
    receive_discover_response(udp_socket, expected_message_seq);
    if (close(udp_socket) < 0)
        syserr("close");
    BOOST_LOG_TRIVIAL(trace) << "Finished discover";
}

multiset<Client::ServerData, std::greater<>> Client::silent_discover_receive(int udp_socket, uint64_t expected_message_seq) {
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

multiset<Client::ServerData, std::greater<>> Client::silent_discover() {
    int udp_socket = create_multicast_udp_socket();
    uint64_t expected_message_seq = send_discover_message(udp_socket);
    auto servers = silent_discover_receive(udp_socket, expected_message_seq);
    if (close(udp_socket) < 0)
        syserr("close");

    return servers;
}

/************************************************** GET FILES LIST ****************************************************/

void Client::display_and_update_files_list(char* data, const char* server_ip) {
    char* filename = strtok(data, "\n");
    while (filename != nullptr) {
        cout << filename << " (" << server_ip << ")" << endl;
        this->last_search_results[filename] = server_ip;
        filename = strtok(nullptr, "\n");
    }
}

uint64_t Client::send_get_files_list_message(int udp_socket, const string &pattern) {
    uint64_t message_seq = generate_message_sequence();
    SimpleMessage message{htobe64(message_seq), cp::files_list_request, pattern.c_str()};
    send_message_multicast_udp(udp_socket, message, pattern.length());
    return message_seq;
}

void Client::receive_search_respond(int udp_socket, uint64_t expected_message_seq) {
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
                message_validation(message, expected_message_seq, cp::files_list_response, recv_len);
                display_and_update_files_list(message.data, source_ip.c_str());
            } catch (const invalid_message &e) {
                cerr << format("[PCKG ERROR] Skipping invalid package from %1%:%2%. %3%\n") %source_ip %source_port %e.what();
                BOOST_LOG_TRIVIAL(error) << format("[PCKG ERROR] Skipping invalid package from %1%:%2%. %3%\n") %source_ip %source_port %e.what();
            }
        }
    }
}

void Client::search(string pattern) {
    BOOST_LOG_TRIVIAL(info) << format("Starting search, pattern=%1%...") %pattern;
    int udp_socket = create_multicast_udp_socket();
    uint64_t message_seq = send_get_files_list_message(udp_socket, pattern);
    receive_search_respond(udp_socket, message_seq);
    if (close(udp_socket) < 0)
        syserr("close");
    BOOST_LOG_TRIVIAL(info) << "Search finished";
}

/**************************************************** FETCH FILE ******************************************************/
uint64_t Client::send_get_file_message(int udp_socket, const char* destination_ip, const string &filename) {
    uint64_t message_sequence = generate_message_sequence();
    SimpleMessage message{htobe64(message_sequence), cp::file_get_request, filename.c_str()};
    send_message_unicast_udp(udp_socket, destination_ip, message, filename.length());
    return message_sequence;
}

// TODO jakiś fałszywy server mógłby się podszyć... trzeba srpawdzić czy otrzymano faktycznie od tego...
in_port_t Client::receive_fetch_file_response(int udp_socket, uint64_t expected_message_seq, const string& expected_filename) {
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
void Client::fetch_file_via_tcp(const string& server_ip, in_port_t server_port, const string& filename) {
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


void Client::fetch(string filename, const string& server_ip) {
    BOOST_LOG_TRIVIAL(trace) << format("Starting fetch, filename = %1%") % filename;
    int unicast_socket = create_unicast_udp_socket();
    uint64_t expected_message_seq = send_get_file_message(unicast_socket, server_ip.c_str(), filename);
    in_port_t tcp_port = receive_fetch_file_response(unicast_socket, expected_message_seq, filename);
    fetch_file_via_tcp(server_ip, tcp_port, filename);
    if (close(unicast_socket) < 0)
        syserr("close");

    BOOST_LOG_TRIVIAL(trace) << "Ending fetch";
}

/*************************************************** UPLOAD FILE ******************************************************/

bool Client::can_upload_file(int udp_socket, const char* server_ip, const string& filename, ComplexMessage& server_response) {
    uint64_t message_sequence = send_upload_file_request(udp_socket, server_ip, filename);
    return receive_upload_file_response(udp_socket, message_sequence, server_response, filename);
}

/// return sent message's message_sequence
uint64_t Client::send_upload_file_request(int udp_socket, const char* destination_ip, const string &filename) {
    uint64_t message_sequence = generate_message_sequence();
    ComplexMessage message{htobe64(message_sequence), cp::file_add_request, filename.c_str(), htobe64(get_file_size(this->out_folder + filename))};
    send_message_unicast_udp(udp_socket, destination_ip, message, filename.length());
    return message_sequence;
}


// TODO wredny server fałszyyw moze wyslac wszystko dobrze ale nie być tym serverem...
bool Client::receive_upload_file_response(int udp_socket, uint64_t expected_message_sequence, ComplexMessage& message, const string& expected_filename) {
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
void Client::upload_file_via_tcp(const char* server_ip, in_port_t server_port, const fs::path& file_path) {
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

bool Client::at_least_on_server_has_space(const multiset<Client::ServerData, std::greater<>>& servers, size_t space_required) {
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
void Client::upload(fs::path file_path) { // copy because it's another thread!
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

/*************************************************** REMOVE FILE ******************************************************/
void Client::remove(string filename) {
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

/**************************************************** PUBLIC **********************************************************/

Client::Client(string mcast_addr, uint16_t cmd_port, string folder, const uint16_t timeout)
    : mcast_addr(std::move(mcast_addr)),
    cmd_port(cmd_port),
    out_folder(std::move(folder)),
    timeout(timeout),
    generator(std::random_device{}())
    {}

Client::Client(ClientConfiguration& configuration)
    : Client(configuration.mcast_addr.c_str(), configuration.cmd_port, configuration.out_folder, configuration.timeout)
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
    std::vector<string> tokenized_input;
    string input, command, param = "";
    getline(std::cin, input);
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

void Client::run() {
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

void Client::init() {

}

std::ostream& operator << (std::ostream &out, const Client &client) {
    out << "\nCLIENT INFO:";
    out << "\nMCAST_ADDR = " << client.mcast_addr;
    out << "\nCMD_PORT = " << client.cmd_port;
    out << "\nFOLDER = " << client.out_folder;
    out << "\nTIMEOUT = " << client.timeout;

    return out;
}