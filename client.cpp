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
    if (!is_valid_data(message.data, message_size - cp::complex_message_no_data_size))
        throw invalid_message("invalid message data");
}

static void message_validation(const SimpleMessage& message, uint64_t expected_message_seq, const string& expected_command, ssize_t message_size, const string& expected_data = "") {
    if (message_size < cp::simple_message_no_data_size)
        throw invalid_message("message too small");
    if (!is_expected_command(message.command, expected_command))
        throw invalid_message("invalid command");
    if (be64toh(message.message_seq) != expected_message_seq)
        throw invalid_message("invalid message seq");
    if (!is_valid_data(message.data, message_size - cp::simple_message_no_data_size))
        throw invalid_message("invalid message data");
    if (!expected_data.empty() && expected_data != message.data)
        throw invalid_message("unexpected data received");
}

static tuple<string, in_port_t> unpack_sockaddr(const struct sockaddr_in& address) {
    return {inet_ntoa(address.sin_addr), be16toh(address.sin_port)};
}

uint64_t Client::generate_message_sequence() {
    return uniform_distribution(generator);
}

/***************************************************** DISCOVER *******************************************************/

static void display_server_discovered_info(const char* server_ip, const char* server_mcast_addr, uint64_t server_space) {
    cout << format("Found %1%(%2%) with free space %3%") %server_ip %server_mcast_addr %server_space << endl;
}

/**
 * @param udp_socket
 * @return Expected respond message's message_seq
 */
uint64_t Client::send_discover_message(udp_socket& sock) {
    uint64_t message_seq = generate_message_sequence();
    SimpleMessage message {htobe64(message_seq), cp::discover_request};
    try {
        sock.send(message, mcast_addr, htobe16(cmd_port), 0);
    } catch (const socket_failure& e) {
        syserr(e.what()); // TODO syserr? what to do if i fail...
    }
    return message_seq;
}

void Client::receive_discover_response(udp_socket& sock, uint64_t expected_message_seq) {
    in_port_t source_port;
    string source_ip;

    sock.set_timeout(0, 1000); // Todo no jes pls?
    auto start_time = std::chrono::high_resolution_clock::now();
    while (!its_timeout(start_time)) {
        try {
            auto [message, message_length, source_address] = sock.recvfrom_complex();
            std::tie(source_ip, source_port) = unpack_sockaddr(source_address);
            message_validation(message, expected_message_seq, cp::discover_response, message_length);
            display_server_discovered_info(inet_ntoa(source_address.sin_addr), message.data, be64toh(message.param));
        } catch (const socket_failure &e) {
            continue; // timeout...
        } catch (const invalid_message &e) {
            cerr << format("[PCKG ERROR] Skipping invalid package from %1%:%2%. %3%\n") %source_ip %source_port %e.what();
            BOOST_LOG_TRIVIAL(error)
                << format("[PCKG ERROR] Skipping invalid package from %1%:%2%. %3%\n") %source_ip %source_port %e.what();
        }
    }
}

void Client::discover() {
    BOOST_LOG_TRIVIAL(trace) << "Starting discover...";
    uint64_t expected_message_seq = send_discover_message(multicast_sock);
    receive_discover_response(multicast_sock, expected_message_seq);
    BOOST_LOG_TRIVIAL(trace) << "Finished discover";
}

multiset<Client::ServerData, std::greater<>> Client::silent_discover_receive(udp_socket& sock, uint64_t expected_message_seq) {
    multiset<ServerData, std::greater<>> servers;
    in_port_t source_port;
    string source_ip;

    sock.set_timeout(0, 1000); // TODO do i want to catch it??
    auto start_time = std::chrono::high_resolution_clock::now();
    while (!its_timeout(start_time)) {
        try {
            auto[message, message_length, source_address] = sock.recvfrom_complex();
            std::tie(source_ip, source_port) = unpack_sockaddr(source_address);
            message_validation(message, expected_message_seq, cp::discover_response, message_length);
            servers.insert(ServerData(inet_ntoa(source_address.sin_addr), be64toh(message.param)));
        } catch (const socket_failure &e) {
            continue; // timeout...
        } catch (const invalid_message &e) {
            cerr << format("[PCKG ERROR] Skipping invalid package from %1%:%2%. %3%\n") % source_ip % source_port %
                    e.what();
            BOOST_LOG_TRIVIAL(error)
                << format("[PCKG ERROR] Skipping invalid package from %1%:%2%. %3%\n") % source_ip % source_port %
                   e.what();
        }
    }

    return servers;
}

multiset<Client::ServerData, std::greater<>> Client::silent_discover() {
    udp_socket sock;
    sock.create_multicast_socket();
    uint64_t expected_message_seq = send_discover_message(sock);
    auto servers = silent_discover_receive(sock, expected_message_seq);
    return servers;
}

/************************************************** GET FILES LIST ****************************************************/

void Client::display_and_update_files_list(char* data, const string& server_ip) {
    std::vector<string> files;
    boost::split(files, data, boost::is_any_of("\n"));
    for (const string& file : files) {
        cout << format("%1% (%2%)") %file %server_ip << endl;
        this->last_search_results[file] = server_ip;
    }
}

void Client::receive_search_respond(uint64_t expected_message_seq) {
    struct SimpleMessage message{};
    struct sockaddr_in source_address{};
    ssize_t message_length;

    multicast_sock.set_timeout(0, 1000); // TODO do i want to catch it??
    auto start_time = std::chrono::high_resolution_clock::now();
    while (!its_timeout(start_time)) {
        try {
            std::tie(message, message_length, source_address) = multicast_sock.recvfrom_simple();
        } catch (const socket_failure &e) {
            continue; // timeout...
        }

        auto [source_ip, source_port] = unpack_sockaddr(source_address);
        try {
            message_validation(message, expected_message_seq, cp::files_list_response, message_length);
            display_and_update_files_list(message.data, source_ip.c_str());
        } catch (const invalid_message &e) {
            cerr << format("[PCKG ERROR] Skipping invalid package from %1%:%2%. %3%\n") %source_ip %source_port %e.what();
            BOOST_LOG_TRIVIAL(error) << format("[PCKG ERROR] Skipping invalid package from %1%:%2%. %3%\n") %source_ip %source_port %e.what();
        }
    }
}

void Client::search(string pattern) {
    BOOST_LOG_TRIVIAL(info) << format("Starting search, pattern=%1%...") %pattern;
    uint64_t message_seq = generate_message_sequence();
    SimpleMessage message{htobe64(message_seq), cp::files_list_request, pattern.c_str()};
    multicast_sock.send(message, mcast_addr, be16toh(cmd_port), pattern.length());
    receive_search_respond(message_seq);
    BOOST_LOG_TRIVIAL(info) << "Search finished";
}

/**************************************************** FETCH FILE ******************************************************/

// TODO co jak timeout?!
/***
 *
 * @param udp_socket
 * @param expected_message_seq
 * @param expected_filename
 * @return 0 if no valid message was received, port on which server is waiting otherwise.
 */
in_port_t Client::receive_fetch_file_response(udp_socket& sock, uint64_t expected_message_seq, const string& expected_filename) {
    struct ComplexMessage message{};
    struct sockaddr_in source_address{};
    ssize_t message_length;

    sock.set_timeout(0, 1000); // TODO define it...
    auto start_time = std::chrono::high_resolution_clock::now();
    while (!its_timeout(start_time)) {
        try {
            std::tie(message, message_length, source_address) = sock.recvfrom_complex();
        } catch (const socket_failure &e) {
            continue; // timeout
        }

        auto [source_ip, source_port] = unpack_sockaddr(source_address);
        try {
            BOOST_LOG_TRIVIAL(info) << format("received: %1% from %2%:%3%") %message %source_ip %source_port;
            message_validation(message, expected_message_seq, cp::file_get_response, message_length, expected_filename);
            BOOST_LOG_TRIVIAL(info)
                << format("fetch file valid response received: port=%1% filename=%2%, source=%3%:%4%")
                   % be64toh(message.param) % message.data % source_ip % source_port;
            return be64toh(message.param);
        } catch (const invalid_message &e) {
            cerr << format("[PCKG ERROR] Skipping invalid package from %1%:%2%. %3%\n") % source_ip % source_port %
                    e.what();
            BOOST_LOG_TRIVIAL(error)
                << format("[PCKG ERROR] Skipping invalid package from %1%:%2%. %3%") % source_ip % source_port %
                   e.what();
        }
    }

    return 0;
}

// TODO error handlign
// TODO informaiton to printf
void Client::fetch_file_via_tcp(const string& server_ip, in_port_t server_port, const string& filename) {
    BOOST_LOG_TRIVIAL(trace) << "Starting downloading file via tcp...";
    tcp_socket sock;
    try {
        sock.create_socket();
        sock.connect(server_ip, htobe16(server_port));
    } catch (const socket_failure& e) {
        msgerr(e.what());
        return;
    }

    fs::path file_path(this->out_folder + filename);
    fs::ofstream file_stream(file_path, std::ofstream::binary); // RAII
    if (file_stream.is_open()) {
        ssize_t read_len;
        char buffer[MAX_BUFFER_SIZE];
        try {
            while ((read_len = sock.read(buffer, sizeof(buffer))) > 0)
                file_stream.write(buffer, read_len);
        } catch (const socket_failure& e) {
            msgerr(e.what());
            return;
        }
    } else {
        cerr << "File creation failure" << endl;
    }

    cout << format("File %1% downloaded (%2%:%3%)") %filename %server_ip %server_port << endl;
    BOOST_LOG_TRIVIAL(trace) << "Downloading file via tcp finished";
}


void Client::fetch(string filename, const string& server_ip) {
    BOOST_LOG_TRIVIAL(trace) << format("Starting fetch, filename = %1%") %filename;
    uint64_t message_sequence = generate_message_sequence();
    SimpleMessage message{htobe64(message_sequence), cp::file_get_request, filename.c_str()};
    udp_socket sock;
    try {
        sock.create_socket();
        sock.send(message, server_ip, htobe16(cmd_port), filename.length());
    } catch (const socket_failure& e) {
        msgerr(e.what());
        return;
    }

    BOOST_LOG_TRIVIAL(trace) << format("sent: %1%, expected = %2%") %message %message_sequence;
    in_port_t tcp_port = receive_fetch_file_response(sock, message_sequence, filename);
    if (tcp_port == 0) {
        // TODO co wypisać ?
        cout << format("File %1% downloading failed (%2%:{port_serwera}) timeout") %filename %server_ip << endl;
    } else {
        fetch_file_via_tcp(server_ip, tcp_port, filename);
    }


    BOOST_LOG_TRIVIAL(trace) << format("Ending fetch, filename = %1%") %filename;
}

/*************************************************** UPLOAD FILE ******************************************************/

tuple<in_port_t, bool> Client::can_upload_file(udp_socket& sock, const string& server_ip, const string& filename, ssize_t file_size) {
    uint64_t message_sequence = generate_message_sequence();
    ComplexMessage message{htobe64(message_sequence), cp::file_add_request, filename.c_str(), htobe64(file_size)};
    sock.send(message, server_ip, htobe16(cmd_port), filename.length());
    return receive_upload_file_response(sock, message_sequence, filename);
}

// TODO wredny server fałszyyw moze wyslac wszystko dobrze ale nie być tym serverem...
tuple<in_port_t, bool> Client::receive_upload_file_response(udp_socket& sock, uint64_t expected_message_sequence, const string& expected_filename) {
    ComplexMessage message;
    struct sockaddr_in source_address {};
    ssize_t message_length;

    sock.set_timeout(0, 1000); //catch?
    auto start_time = std::chrono::high_resolution_clock::now();
    while (!its_timeout(start_time)) {
        try {
            std::tie(message, message_length, source_address) = sock.recvfrom_complex();
        } catch (const socket_failure &e) {
            continue; // timeout
        }

        try {
            if (is_expected_command(message.command, cp::file_add_acceptance)) {
                message_validation(message, expected_message_sequence, cp::file_add_acceptance, message_length);
                return {be64toh(message.param), true}; // connect_me
            } else {
                auto message_simple = (SimpleMessage *) &message;
                message_validation(*message_simple, expected_message_sequence, cp::file_add_refusal, message_length, expected_filename);
                return {be64toh(message.param), false}; // no_way
            }
        } catch (const invalid_message &e) {
            auto[source_ip, source_port] = unpack_sockaddr(source_address);
            cerr << format("[PCKG ERROR] Skipping invalid package from %1%:%2%. %3%") % source_ip % source_port %e.what();
            BOOST_LOG_TRIVIAL(error)
                << format("[PCKG ERROR] Skipping invalid package from %1%:%2%. %3%") % source_ip %
                   source_port % e.what();
        }
    }

/// FALSE -> no valid response received
    return {be64toh(message.param), false};
}

// TODO error handling...
void Client::upload_file_via_tcp(const char* server_ip, in_port_t server_port, const fs::path& file_path) {
    BOOST_LOG_TRIVIAL(trace) << format("Uploading file via tcp, port:%1%, file = %2%") %server_port %file_path;
    tcp_socket sock;
    try {
        sock.create_socket();
        sock.connect(server_ip, htobe16(server_port));
    } catch (const socket_failure& e) {
        msgerr(e.what());
        return;
    }

    std::ifstream file_stream {file_path.c_str(), std::ios::binary};
    if (file_stream.is_open()) {
        char buffer[MAX_BUFFER_SIZE];
        while (file_stream) {
            file_stream.read(buffer, MAX_BUFFER_SIZE);
            ssize_t length = file_stream.gcount(); // TODO what for?

            try {
                sock.write(buffer, sizeof(buffer));
            } catch (const socket_failure &e) {
                msgerr(e.what());
                return;
            }
        }
    } else {
        cerr << "File opening error" << endl; // TODO
    }

    BOOST_LOG_TRIVIAL(info) << format("File %1% uploaded (%2%:%3%)") %file_path.filename() %server_ip %server_port;
    cout << format("File %1% uploaded (%2%:%3%)") %file_path.filename() %server_ip %server_port << endl;
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
void Client::upload(fs::path file_path) {
    BOOST_LOG_TRIVIAL(trace) << format("Starting upload procedure, file = %1%") %file_path;
    sleep(5);
    if (!fs::exists(file_path) || !fs::is_regular_file(file_path)) {
        cout << format("File %1% does not exist") %file_path << endl;
        BOOST_LOG_TRIVIAL(trace) << format("Invalid file, file_path = %1%") %file_path;
    } else {
        multiset<ServerData, std::greater<>> servers = silent_discover();

        if (at_least_on_server_has_space(servers, fs::file_size(file_path))) {
            string filename = file_path.filename().string();
            udp_socket sock;
            try {
                sock.create_socket();
            } catch (const socket_failure& e) {
                msgerr(e.what());
                return;
            }

            for (const auto &server : servers) {
                auto [port, is_approval] = can_upload_file(sock, server.ip_addr, filename, fs::file_size(file_path));
                if (is_approval) {
                    upload_file_via_tcp(server.ip_addr, port, file_path);
                    return;
                }
            }
        }
        cout << format("File %1% too big %1%") %file_path << endl;
        BOOST_LOG_TRIVIAL(trace) << format("File %1% too big %1%") %file_path;
    }
}

/*************************************************** REMOVE FILE ******************************************************/
void Client::remove(string filename) {
    BOOST_LOG_TRIVIAL(trace) << "Starting remove file";
    SimpleMessage message {htobe64(generate_message_sequence()), cp::file_remove_request, filename.c_str()};
    try {
        udp_socket sock;
        sock.create_multicast_socket();
        sock.send(message, mcast_addr, htobe16(cmd_port), filename.length());
        BOOST_LOG_TRIVIAL(trace) << "Ending remove file";
    } catch (const socket_failure& e) {
        msgerr(e.what());
    }
}

/**************************************************** PUBLIC **********************************************************/

Client::Client(string mcast_addr, uint16_t cmd_port, string folder, const uint16_t timeout)
    : mcast_addr(std::move(mcast_addr)),
    cmd_port(cmd_port),
    out_folder(*folder.rend() == '/' ? std::move(folder) : std::move(folder) + "/"),
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
                        cout << "Unknown filename" << endl;
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
    fs::create_directories(out_folder);
    multicast_sock.create_multicast_socket();
}

std::ostream& operator << (std::ostream &out, const Client &client) {
    out << "\nCLIENT INFO:";
    out << "\nMCAST_ADDR = " << client.mcast_addr;
    out << "\nCMD_PORT = " << client.cmd_port;
    out << "\nFOLDER = " << client.out_folder;
    out << "\nTIMEOUT = " << client.timeout;

    return out;
}