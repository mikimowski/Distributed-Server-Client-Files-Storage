#include <chrono>
#include <random>
#include <fstream>
#include <set>

#include <boost/algorithm/string.hpp>
#include <boost/filesystem.hpp>
#include <boost/format.hpp>
#include <boost/log/core.hpp>
#include <boost/log/trivial.hpp>
#include <boost/log/utility/setup/file.hpp>
#include <boost/log/utility/setup/common_attributes.hpp>

#include <unistd.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/socket.h>

#include "client.h"
#include "../helper.h"
#include "../logger.h"

using std::string;
using std::vector;
using boost::format;
using std::tuple;

namespace cp = communication_protocol;
namespace fs = boost::filesystem;
namespace logging = boost::log;

uint64_t Client::generate_message_sequence() {
    return uniform_distribution(generator);
}

/***************************************************** DISCOVER *******************************************************/

void Client::receive_discover_response(udp_socket& sock, uint64_t expected_message_seq) {
    ComplexMessage message;
    ssize_t message_length;
    struct sockaddr_in source_address{};

    auto start_time = std::chrono::steady_clock::now();
    while (!is_timeout(start_time, timeout)) {
        try {
            sock.set_timeout(get_elapsed_time_timeval(start_time, timeout));
            std::tie(message, message_length, source_address) = sock.recvfrom_complex();
        } catch (const socket_failure &e) {
            continue; // timeout...
        }

        auto [source_ip, source_port] = unpack_sockaddr(source_address);
        try {
            message_validation(message, expected_message_seq, cp::discover_response, message_length);
            logger::server_discovery(inet_ntoa(source_address.sin_addr), message.data, be64toh(message.param));
        } catch (const invalid_message &e) {
            logger::package_skipping(source_ip, source_port, e.what());
        }
    }
}

void Client::discover() {
    uint64_t message_seq = generate_message_sequence();
    SimpleMessage message {htobe64(message_seq), cp::discover_request};
    try {
        multicast_sock.send(message, mcast_addr, htobe16(cmd_port), 0);
    } catch (const socket_failure& e) {
        logger::syserr(e.what());
        return;
    }
    receive_discover_response(multicast_sock, message_seq);
}

vector<Client::ServerData> Client::silent_discover_receive(udp_socket& sock, uint64_t expected_message_seq) {
    vector<ServerData> servers;
    ComplexMessage message;
    ssize_t message_length;
    struct sockaddr_in source_address{};

    auto start_time = std::chrono::steady_clock::now();
    while (!is_timeout(start_time, timeout)) {
        try {
            sock.set_timeout(get_elapsed_time_timeval(start_time, timeout));
            std::tie(message, message_length, source_address) = sock.recvfrom_complex();
        } catch (const socket_failure &e) {
            continue; // timeout...
        }

        auto [source_ip, source_port] = unpack_sockaddr(source_address);
        try {
            message_validation(message, expected_message_seq, cp::discover_response, message_length);
            servers.emplace_back(ServerData(inet_ntoa(source_address.sin_addr), be64toh(message.param)));
        } catch (const invalid_message &e) {
            logger::package_skipping(source_ip, source_port, e.what());
        }
    }

    return servers;
}

vector<Client::ServerData> Client::silent_discover() {
    uint64_t message_seq = generate_message_sequence();
    SimpleMessage message {htobe64(message_seq), cp::discover_request};
    udp_socket sock;
    try {
        sock.create_multicast_socket();
        sock.send(message, mcast_addr, htobe16(cmd_port), 0);
    } catch (const socket_failure& e) {
        logger::syserr(e.what());
        return vector<ServerData>{}; // empty vector
    }
    auto servers = silent_discover_receive(sock, message_seq);
    return servers;
}

/************************************************** GET FILES LIST ****************************************************/

void Client::update_search_results(const std::vector<string>& files, const string& server_ip) {
    for (const string& file : files)
        last_search_results[file] = server_ip;
}

void Client::receive_search_respond(uint64_t expected_message_seq) {
    SimpleMessage message{};
    struct sockaddr_in source_address{};
    ssize_t message_length;

    auto start_time = std::chrono::steady_clock::now();
    while (!is_timeout(start_time, timeout)) {
        try {
            multicast_sock.set_timeout(get_elapsed_time_timeval(start_time, timeout));
            std::tie(message, message_length, source_address) = multicast_sock.recvfrom_simple();
        } catch (const socket_failure &e) {
            continue; // timeout...
        }

        auto [source_ip, source_port] = unpack_sockaddr(source_address);
        try {
            message_validation(message, expected_message_seq, cp::files_list_response, message_length);
            std::vector<string> files;
            boost::split(files, message.data, boost::is_any_of("\n"));
            update_search_results(files, source_ip);
            logger::display_files_list(files, source_ip);
        } catch (const invalid_message &e) {
            logger::package_skipping(source_ip, source_port, e.what());
        }
    }
}

void Client::search(string pattern) {
    BOOST_LOG_TRIVIAL(info) << format("Starting search, pattern=%1%") %pattern;
    last_search_results.clear();
    uint64_t message_seq = generate_message_sequence();
    SimpleMessage message{htobe64(message_seq), cp::files_list_request, pattern.c_str()};
    try {
        multicast_sock.send(message, mcast_addr, be16toh(cmd_port), pattern.length());
    } catch (const socket_failure& e) {
        logger::syserr(e.what());
        return;
    }
    receive_search_respond(message_seq);
}

/**************************************************** FETCH FILE ******************************************************/

in_port_t Client::receive_fetch_file_response(udp_socket& sock, uint64_t expected_message_seq, const string& expected_filename) {
    struct ComplexMessage message{};
    struct sockaddr_in source_address{};
    ssize_t message_length;

    auto start_time = std::chrono::steady_clock::now();
    while (!is_timeout(start_time, timeout)) {
        try {
            sock.set_timeout(get_elapsed_time_timeval(start_time, timeout));
            std::tie(message, message_length, source_address) = sock.recvfrom_complex();
        } catch (const socket_failure &e) {
            continue; // timeout
        }

        auto [source_ip, source_port] = unpack_sockaddr(source_address);
        try {
            BOOST_LOG_TRIVIAL(info) << format("received: %1% from %2%:%3%") %message %source_ip %source_port;
            message_validation(message, expected_message_seq, cp::file_get_response, message_length, expected_filename);
            return be64toh(message.param);
        } catch (const invalid_message &e) {
            logger::package_skipping(source_ip, source_port, e.what());
        }
    }

    return 0;
}

void Client::fetch_file_via_tcp(const string& server_ip, in_port_t server_port, const string& filename) {
    BOOST_LOG_TRIVIAL(trace) << format("Starting downloading file via tcp, filename = %1%") %filename;
    tcp_socket sock;
    try {
        sock.create_socket();
        sock.connect(server_ip, htobe16(server_port));
    } catch (const socket_failure& e) {
        logger::file_fetch_failure(filename, server_ip, server_port, e.what());
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
        } catch (const std::exception& e) {
            logger::file_fetch_failure(filename, server_ip, server_port, e.what());
            return;
        }
    } else {
        logger::file_fetch_failure(filename, server_ip, server_port, "file opening failed");
    }

    logger::file_fetch_success(filename, server_ip, server_port);
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
        logger::file_fetch_failure(filename, server_ip, 0, e.what());
        return;
    }

    in_port_t tcp_port = receive_fetch_file_response(sock, message_sequence, filename);
    if (tcp_port == 0) {
        logger::file_fetch_failure(filename, server_ip, 0, "timeout");
    } else {
        fetch_file_via_tcp(server_ip, tcp_port, filename);
    }
}

/*************************************************** UPLOAD FILE ******************************************************/

tuple<in_port_t, bool> Client::can_upload_file(udp_socket& sock, const string& server_ip, const string& filename, ssize_t file_size) {
    uint64_t message_sequence = generate_message_sequence();
    ComplexMessage message{htobe64(message_sequence), cp::file_add_request, filename.c_str(), htobe64(file_size)};
    try {
        sock.send(message, server_ip, htobe16(cmd_port), filename.length());
    } catch (const socket_failure& e) {
        logger::syserr(e.what());
        return {0, false};
    }
    return receive_upload_file_response(sock, message_sequence, filename);
}

tuple<in_port_t, bool> Client::receive_upload_file_response(udp_socket& sock, uint64_t expected_message_sequence, const string& expected_filename) {
    ComplexMessage message;
    struct sockaddr_in source_address {};
    ssize_t message_length;

    auto start_time = std::chrono::steady_clock::now();
    while (!is_timeout(start_time, timeout)) {
        try {
            sock.set_timeout(get_elapsed_time_timeval(start_time, timeout));
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
            auto [source_ip, source_port] = unpack_sockaddr(source_address);
            logger::package_skipping(source_ip, source_port, e.what());
        }
    }

    return {0, false}; // no valid response received
}

void Client::upload_file_via_tcp(const char* server_ip, in_port_t server_port, const fs::path& file_path, uint64_t file_size) {
    BOOST_LOG_TRIVIAL(trace) << format("Uploading file via tcp, port:%1%, file = %2%") %server_port %file_path;
    string filename = file_path.filename().string();
    tcp_socket sock;
    try {
        sock.create_socket();
        sock.connect(server_ip, htobe16(server_port));
    } catch (const socket_failure& e) {
        logger::file_upload_failure(filename, server_ip, server_port, e.what());
        return;
    }

    std::ifstream file_stream {file_path.c_str(), std::ios::binary};
    uint64_t to_upload = file_size;
    ssize_t length;
    if (file_stream.is_open()) {
        char buffer[MAX_BUFFER_SIZE];
        while (file_stream) {
            try {
                file_stream.read(buffer, MAX_BUFFER_SIZE);
                length = file_stream.gcount();
                to_upload -= length;
                sock.write(buffer, length);
            } catch (const std::exception &e) {
                logger::file_upload_failure(filename, server_ip, server_port, e.what());
                return;
            }
        }

        if (to_upload != 0) {
            logger::file_upload_failure(filename, server_ip, server_port, "partial upload");
            return;
        }
    } else {
        logger::file_upload_failure(filename, server_ip, server_port, "file opening failed");
        return;
    }

    logger::file_upload_success(file_path.filename().string(), server_ip, server_port);
}

bool Client::at_least_on_server_has_space(const vector<Client::ServerData>& servers, size_t space_required) {
    return !servers.empty() && servers[0].available_space >= space_required;
}

void Client::upload(fs::path file_path) {
    BOOST_LOG_TRIVIAL(trace) << format("Starting upload procedure, file = %1%") %file_path;
    if (!fs::exists(file_path) || !fs::is_regular_file(file_path)) {
        logger::file_not_exist(file_path.filename().string());
    } else {
        vector<ServerData> servers = silent_discover();

        if (at_least_on_server_has_space(servers, fs::file_size(file_path))) {
            string filename = file_path.filename().string();
            udp_socket sock;
            try {
                sock.create_socket();
            } catch (const socket_failure& e) {
                logger::syserr(e.what());
                return;
            }

            uint64_t file_size = fs::file_size(file_path);
            for (const auto &server : servers) {
                auto [port, is_approval] = can_upload_file(sock, server.ip_addr, filename, file_size);
                if (is_approval) {
                    upload_file_via_tcp(server.ip_addr, port, file_path, file_size);
                    return;
                }
            }
        }

        logger::file_too_big(file_path.filename().string());
    }
}

/*************************************************** REMOVE FILE ******************************************************/
void Client::remove(const string& filename) {
    BOOST_LOG_TRIVIAL(trace) << format("Remove file, filename = %1%") %filename;
    SimpleMessage message {htobe64(generate_message_sequence()), cp::file_remove_request, filename.c_str()};
    try {
        udp_socket sock;
        sock.create_multicast_socket();
        sock.send(message, mcast_addr, htobe16(cmd_port), filename.length());
    } catch (const socket_failure& e) {
        logger::syserr(e.what());
    }
}

/**************************************************** PUBLIC **********************************************************/

Client::Client(string mcast_addr, uint16_t cmd_port, string folder, uint16_t timeout)
    : mcast_addr(std::move(mcast_addr)),
    cmd_port(cmd_port),
    out_folder(*folder.rend() == '/' ? std::move(folder) : std::move(folder) + "/"),
    timeout(timeout),
    generator(std::random_device{}())
    {}

Client::Client(ClientConfiguration& configuration)
    : Client(configuration.mcast_addr, configuration.cmd_port, configuration.out_folder, configuration.timeout)
    {}


void Client::init() {
    if (out_folder != "./" && out_folder != "../")
        fs::create_directories(out_folder);
    multicast_sock.create_multicast_socket();
}

static bool is_param_required(const string& command) {
    std::set<string> s = {"fetch", "upload", "remove"};
    return s.find(command) != s.end();
}

static bool no_parameter_allowed(const string& command) {
    std::set<string> s {"exit", "discover"};
    return s.find(command) != s.end();
}

/**
 * @return <command, parameter> command and parameter given by user,
 * if parameter is optional and wasn't given then empty string is returned
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
                        logger::message_cout("Unknown filename");
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

std::ostream& operator << (std::ostream &out, const Client &client) {
    out << "\nCLIENT INFO:";
    out << "\nMCAST_ADDR = " << client.mcast_addr;
    out << "\nCMD_PORT = " << client.cmd_port;
    out << "\nFOLDER = " << client.out_folder;
    out << "\nTIMEOUT = " << client.timeout;

    return out;
}