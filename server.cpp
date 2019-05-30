#include <iostream>
#include <utility>
#include <vector>
#include <random>
#include <thread>
#include <regex>
#include <mutex>
#include <chrono>

#include <boost/filesystem.hpp>
#include <boost/algorithm/string.hpp>
#include <boost/format.hpp>
#include <boost/log/core.hpp>
#include <boost/log/trivial.hpp>
#include <boost/log/utility/setup/file.hpp>
#include <boost/log/utility/setup/common_attributes.hpp>

#include <unistd.h>
#include <stdint.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>

#include "server.h"
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

using std::string;
using std::cout;
using std::endl;
using std::cerr;
using std::to_string;
using std::vector;
using std::string_view;
using std::tuple;
using std::ostream;
using boost::format;
using std::exception;
using std::thread;

namespace fs = boost::filesystem;
namespace cp = communication_protocol;
namespace logging = boost::log;

bool Server::add_file_to_storage(const string& filename) {
    std::lock_guard<std::mutex> lock(files_in_storage_mutex);
    return files_in_storage.insert(filename).second;
}

/**
  * Removes filename from set of filenames in storage
  * @param filename filename to be removed from set
  * @return Number of elements erased
  */
int Server::remove_file_from_storage(const string &filename) {
    std::lock_guard<std::mutex> lock(files_in_storage_mutex);
    return files_in_storage.erase(filename);
}

bool Server::is_in_storage(const string& filename) {
    std::lock_guard<std::mutex> lock(files_in_storage_mutex);
    return files_in_storage.find(filename) != files_in_storage.end();
}

void Server::generate_files_in_storage() {
    if (!fs::exists(this->shared_folder))
        throw std::invalid_argument("Shared folder doesn't exists");
    if (!fs::is_directory(this->shared_folder))
        throw std::invalid_argument("Shared folder is not a directory");
    for (const auto& entry: fs::directory_iterator(this->shared_folder)) {
        if (fs::is_regular_file(entry)) {
            files_in_storage.insert(entry.path().filename().string());
            this->used_space += fs::file_size(entry.path());
        }
    }
}

// TODO: Ask whether directories should be listed
void Server::init_sockets() {
    if ((recv_socket = socket(AF_INET, SOCK_DGRAM, 0)) < 0)
        syserr("socket");
    if ((udp_send_socket = socket(AF_INET, SOCK_DGRAM, 0)) < 0)
        syserr("socket");

    join_multicast_group();

    int reuse = 1; // TODO wywalić domyślnie?
    if (setsockopt(recv_socket, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) < 0)
        syserr("setsockopt(SO_REUSEADDR) failed");
    reuse = 1;
    if (setsockopt(udp_send_socket, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) < 0)
        syserr("setsockopt(SO_REUSEADDR) failed");

    struct sockaddr_in local_address{};
    local_address.sin_family = AF_INET;
    local_address.sin_addr.s_addr = htonl(INADDR_ANY);
    local_address.sin_port = htons(cmd_port);
    if (bind(recv_socket, (struct sockaddr*) &local_address, sizeof(local_address)) < 0)
        syserr("bind");
}

void Server::join_multicast_group() {
    struct ip_mreq ip_mreq;
    ip_mreq.imr_interface.s_addr = htonl(INADDR_ANY);
    if (inet_aton(this->multicast_address.c_str(), &ip_mreq.imr_multiaddr) == 0)
        syserr("inet_aton");
    if (setsockopt(recv_socket, IPPROTO_IP, IP_ADD_MEMBERSHIP, (void*)&ip_mreq, sizeof ip_mreq) < 0)
        syserr("setsockopt");
}

void Server::leave_multicast_group() {
    struct ip_mreq ip_mreq;
    ip_mreq.imr_interface.s_addr = htonl(INADDR_ANY);
    if (inet_aton(this->multicast_address.c_str(), &ip_mreq.imr_multiaddr) == 0)
        syserr("inet_aton");
    if (setsockopt(recv_socket, IPPROTO_IP, IP_DROP_MEMBERSHIP, (void*)&ip_mreq, sizeof ip_mreq) < 0)
        syserr("setsockopt");
}

void Server::send_message_udp(const SimpleMessage &message, const struct sockaddr_in& destination_address, uint16_t data_length) {
    uint16_t message_length = cp::simple_message_no_data_size + data_length;
    if (sendto(udp_send_socket, &message, message_length, 0, (struct sockaddr*) &destination_address, sizeof(destination_address)) != message_length)
        msgerr("sendto");
    BOOST_LOG_TRIVIAL(info) << "UDP command sent";
}

void Server::send_message_udp(const ComplexMessage &message, const struct sockaddr_in& destination_address, uint16_t data_length) {
    uint16_t message_length = cp::complex_message_no_data_size + data_length;
    if (sendto(udp_send_socket, &message, message_length, 0, (struct sockaddr*) &destination_address, sizeof(destination_address)) != message_length)
        msgerr("sendto");
    BOOST_LOG_TRIVIAL(info) << "UDP command sent";
}

// TODO jak ktoś tego uzywa to check czy potem jest tcp_socket >= 0 !!
static tuple<int, in_port_t> create_tcp_socket() {
    int tcp_socket;
    struct sockaddr_in local_addr {};
    socklen_t addrlen = sizeof(local_addr);
    memset(&local_addr, 0, sizeof(local_addr)); // sin_port set to 0, therefore it will be set random free port
    local_addr.sin_family = AF_INET;
    local_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    if ((tcp_socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)) < 0)
        msgerr("socket");
    if (bind(tcp_socket, (struct sockaddr*) &local_addr, sizeof(local_addr)) < 0)
        msgerr("bind");
    if (listen(tcp_socket, TCP_QUEUE_LENGTH) < 0)
        msgerr("listen");
    if (getsockname(tcp_socket, (struct sockaddr*) &local_addr, &addrlen) < 0)
        msgerr("getsockname");
    in_port_t tcp_port = be16toh(local_addr.sin_port);

    BOOST_LOG_TRIVIAL(info) << "TCP socket created, port chosen = " << tcp_port;
    return {tcp_socket, tcp_port};
}


uint64_t Server::get_available_space() {
    uint64_t tmp = this->used_space; // threads...
    return tmp > this->max_available_space ? 0 : this->max_available_space - tmp;
}

/**
 * Attempts to reserve given amount of space
 * @param size size of space to be reserved
 * @return True if space was reserved successfuly, false otherwise.
 */
bool Server::check_and_reserve_space(uint64_t size) {
    std::lock_guard<std::mutex> lock(used_space_mutex);
    if (size > max_available_space || used_space > max_available_space - size)
        return false;
    used_space += size;
    return true;
}

/**
 * Use it correctly!
 */
void Server::free_space(uint64_t size) {
    std::lock_guard<std::mutex> lock(used_space_mutex);
    used_space -= size;
}


/**************************************************** DISCOVER ********************************************************/

// TODO multicast ma być w jakim formacie przesłany??
void Server::handle_discover_request(const struct sockaddr_in& destination_address, uint64_t message_seq) {
    struct ComplexMessage message {htobe64(message_seq), cp::discover_response,
                                   this->multicast_address.c_str(), htobe64(this->get_available_space())};
    BOOST_LOG_TRIVIAL(info) << format("Sending to: %1%:%2%")
                               %inet_ntoa(destination_address.sin_addr) %be16toh(destination_address.sin_port);
    send_message_udp(message, destination_address, this->multicast_address.length());
    BOOST_LOG_TRIVIAL(info) << "Message sent: " << message;
}

/*************************************************** FILES LIST *******************************************************/

static void fill_message_with_filename(struct SimpleMessage& msg_send, uint64_t* start_index, const string& filename) {
    if (*start_index > 0)
        msg_send.data[(*start_index)++] = '\n';
    strcpy(msg_send.data + *start_index, filename.c_str());
    *start_index += filename.length();
}

void Server::handle_files_list_request(struct sockaddr_in destination_address, uint64_t message_seq, string pattern) {
    BOOST_LOG_TRIVIAL(trace) << format("Files list request for pattern: %1%") %pattern;
    uint64_t curr_data_len = 0;
    struct SimpleMessage message {htobe64(message_seq), cp::files_list_response};

    std::lock_guard<std::mutex> lock(files_in_storage_mutex);
    for (const string& filename : this->files_in_storage) {
        if (is_substring(pattern, filename)) {
            if (cp::max_simple_data_size > curr_data_len + filename.length()) {
                fill_message_with_filename(message, &curr_data_len, filename);
            } else {
                send_message_udp(message, destination_address, curr_data_len);
                curr_data_len = 0;
                memset(&message.data, '\0', sizeof(message.data));
            }
        }
    }
    if (curr_data_len > 0)
        send_message_udp(message, destination_address, curr_data_len);
    BOOST_LOG_TRIVIAL(trace) << format("Handled files list request, pattern = %1%") %pattern;
}

/************************************************** DOWNLOAD FILE *****************************************************/

/**
 * 1. Select free port
 * 2. Send chosen port to the client
 * 3. Wait on this port for TCP connection with the client
 */
void Server::handle_file_request(struct sockaddr_in destination_address, uint64_t message_seq, string filename) {
    BOOST_LOG_TRIVIAL(info)  << format("Starting file request, filename = %1%") %filename;
    if (is_in_storage(filename)) {
        auto [tcp_socket, tcp_port] = create_tcp_socket();

        ComplexMessage message{htobe64(message_seq), cp::file_get_response, filename.c_str(), htobe64(tcp_port)};
        send_message_udp(message, destination_address, filename.length());
        BOOST_LOG_TRIVIAL(info) << format("TCP port info sent, port chosen = %1%") %tcp_port;
        send_file_via_tcp(tcp_socket, tcp_port, filename);

        if (close(tcp_socket) < 0)
            msgerr("close");
    } else {
        cout << "Incorrect file request received" << endl; // TODO co wypisywać?
    }
    BOOST_LOG_TRIVIAL(info)  << format("Handled file request, filename = %1%") %filename;
}

void Server::send_file_via_tcp(int tcp_socket, uint16_t tcp_port, const string& filename) {
    BOOST_LOG_TRIVIAL(trace) << format("Starting sending file, port = %1%, filename = %2%") %tcp_port %filename;

    fd_set set;
    struct timeval timeval{};
    FD_ZERO(&set);
    FD_SET(tcp_socket, &set);

    timeval.tv_sec = this->timeout;
    int rv = select(tcp_socket + 1, &set, nullptr, nullptr, &timeval); // Wait for client up to timeout sec.
    if (rv == -1) {
        msgerr("select, port = " + to_string(tcp_port));
    } else if (rv == 0) {
        BOOST_LOG_TRIVIAL(info) << format("Timeout on port:%1% no message received") %tcp_port;
    } else { // Client is waiting
        int sock;
        struct sockaddr_in source_address{};
        memset(&source_address, 0, sizeof(source_address));
        socklen_t len = sizeof(source_address);

        if ((sock = accept(tcp_socket, (struct sockaddr *) &source_address, &len)) < 0) {
            msgerr("accept, port = " + to_string(tcp_port));
        } else {
            fs::path file_path{this->shared_folder + filename};
            std::ifstream file_stream{file_path.c_str(), std::ios::binary};

            if (file_stream.is_open()) {
                char buffer[MAX_BUFFER_SIZE];
                while (file_stream) {
                    file_stream.read(buffer, MAX_BUFFER_SIZE);
                    ssize_t length = file_stream.gcount();
                    if (write(sock, buffer, length) != length) {
                        msgerr("write, port = " + to_string(tcp_port));
                        break;
                    }
                }
                file_stream.close();
                BOOST_LOG_TRIVIAL(info) << format("Sending file via tcp finished, port = %1%") % tcp_port;
            } else {
                BOOST_LOG_TRIVIAL(error)
                    << format("File opening error, filename = %1%, port = %2%") % filename % tcp_port << endl;
            }

            if (close(sock) < 0)
                msgerr("close, port = " + to_string(tcp_port));
        }
    }
}


/******************************************************* UPLOAD *******************************************************/

/* TODO file_size = 0?
 *
 *
 * */

void Server::handle_upload_request(struct sockaddr_in destination_address, uint64_t message_seq,
                           string filename, uint64_t file_size) {
    BOOST_LOG_TRIVIAL(info) << format("File upload request, filename = %1%, filesize = %2%") %filename %file_size;
    bool can_upload;
    string rejection_reason;

    if (!(can_upload = check_and_reserve_space(file_size))) {
        rejection_reason = "not enough space";
    } else if (!(can_upload = add_file_to_storage(filename))) {
        rejection_reason = "file already in storage";
    } else {
        BOOST_LOG_TRIVIAL(info) << format("Accepting file upload request, filename = %1%, filesize = %2%") %filename %file_size;
        auto[tcp_socket, tcp_port] = create_tcp_socket();

        ComplexMessage message {htobe64(message_seq), cp::file_add_acceptance, filename.c_str(), htobe64(tcp_port)};
        send_message_udp(message, destination_address);   // Send info about tcp port
        upload_file_via_tcp(tcp_socket, tcp_port, filename);
    }

    if (!can_upload) {
        BOOST_LOG_TRIVIAL(info) << format("Rejecting file upload request, filename = %1%, filesize = %2%, %3%") %filename %file_size %rejection_reason;
        SimpleMessage message{htobe64(message_seq), cp::file_add_refusal};
        send_message_udp(message, destination_address);
    }
    BOOST_LOG_TRIVIAL(info) << format("Handled file upload request, filename = %1%, filesize = %2%") %filename %file_size;
}

// TODO error handling
void Server::upload_file_via_tcp(int tcp_socket, uint16_t port, const string& filename) {
    BOOST_LOG_TRIVIAL(trace) << format("Uploading file via tcp, port:%1%") %port;

    fd_set set;
    struct timeval timeval{};
    FD_ZERO(&set);
    FD_SET(tcp_socket, &set);

    timeval.tv_sec = this->timeout;
    int rv = select(tcp_socket + 1, &set, nullptr, nullptr, &timeval);
    if (rv == -1) {
        BOOST_LOG_TRIVIAL(error) << "select";
    } else if (rv == 0) {
        BOOST_LOG_TRIVIAL(info) << format("Timeout on port:%1% no message received") %port;
    } else {
        struct sockaddr_in source_address{};
        memset(&source_address, 0, sizeof(source_address));
        socklen_t len = sizeof(source_address);
        int sock = accept(tcp_socket, (struct sockaddr *) &source_address, &len);

        fs::path file_path(this->shared_folder + filename);
        fs::ofstream ofs(file_path, std::ofstream::binary);

        char buffer[MAX_BUFFER_SIZE];
        while ((len = read(sock, buffer, sizeof(buffer))) > 0) {
            ofs.write(buffer, len);
        }
        ofs.close();

        // TODO if failure ~ remove filename, free space

        if (close(sock) < 0)
            msgerr("close");

        BOOST_LOG_TRIVIAL(trace) << format("Ending uploading file via tcp, port:%1%") %port;
    }
}


/***************************************************** REMOVE *********************************************************/

void Server::handle_remove_request(string filename) {
    if (this->remove_file_from_storage(filename) > 0) {
        BOOST_LOG_TRIVIAL(info) << format("Deleting file, filename = %1%") %filename;
        fs::path file_path = this->shared_folder + filename;

        try {
            free_space(fs::file_size(file_path));
            fs::remove(file_path);
        } catch (const exception& e) {
            BOOST_LOG_TRIVIAL(error) << format("File deletion error, filename = %1%, %2%") %filename %e.what();
        }
        BOOST_LOG_TRIVIAL(info) << format("File successfully deleted, filename = %1%") %filename;
    } else {
        BOOST_LOG_TRIVIAL(info) << format("Skipping deleting file, no such file in storage, filename = %1%") %filename;
    }
}

/*************************************************** VALIDATION *******************************************************/

/**
 * Basic message validation:
 * Assures that message's command is correct and data if required was attached. Doesn't validate data.
 * 1. Checks whether received command is valid.
 * 2. Checks whether message length is in acceptable range.
 * @param message - message to be validated.
 * @param message_length - length of given message (bytes read from socket).
 */
static void message_validation(const ComplexMessage& message, ssize_t message_length) {
    if (is_valid_string(message.command, cp::max_command_length)) {
        if (message.command == cp::file_add_request) {
            if (message_length < cp::complex_message_no_data_size)
                throw invalid_message("Message too small");
        } else {
            if (message_length < cp::simple_message_no_data_size)
                throw invalid_message("Message too small");

            if (message.command == cp::discover_request) {
                if (message_length > cp::simple_message_no_data_size)
                    throw invalid_message("Message too big");
            } else if (message.command == cp::files_list_request) {
// TODO ?
            } else if (message.command == cp::file_add_request) {
                if (message_length == cp::simple_message_no_data_size)
                    throw invalid_message("Upload request, filename not specified.");
            } else if (message.command == cp::file_get_request) {
                if (message_length == cp::simple_message_no_data_size)
                    throw invalid_message("Fetch request, filename not specified.");
            } else if (message.command == cp::file_remove_request) {
                if (message_length == cp::simple_message_no_data_size)
                    throw invalid_message("Remove request, filename not specified.");
            } else {
                throw invalid_message("Unknown command");
            }
        }
    } else {
        throw invalid_message("Invalid command");
    }
}

/******************************************************** RUN *********************************************************/

/**
 *
 * @return (message received, length of received message in bytes, source address)
 */
 // TODO być może timeout... i sprawdzanie czy serveer is running...
tuple<ComplexMessage, ssize_t, struct sockaddr_in> Server::receive_next_message() {
    ComplexMessage message;
    ssize_t recv_len;
    struct sockaddr_in source_address{};
    socklen_t addrlen = sizeof(struct sockaddr_in);

    recv_len = recvfrom(recv_socket, &message, sizeof(message), 0, (struct sockaddr*) &source_address, &addrlen);
    if (recv_len < 0)
        msgerr("read");

    return {message, recv_len, source_address};
}

/****************************************************** PUBLIC ********************************************************/

Server::Server(string mcast_addr, in_port_t cmd_port, uint64_t max_available_space, string shared_folder_path, uint16_t timeout)
    : multicast_address(std::move(mcast_addr)),
    cmd_port(cmd_port),
    max_available_space(max_available_space),
    shared_folder(move(shared_folder_path)),
    timeout(timeout)
    {}

Server::Server(const ServerConfiguration& server_configuration)
    : Server(server_configuration.mcast_addr, server_configuration.cmd_port, server_configuration.max_space,
            server_configuration.shared_folder, server_configuration.timeout)
    {}

void Server::init() {
    BOOST_LOG_TRIVIAL(trace) << "Starting server initialization...";
    generate_files_in_storage();
    init_sockets();
    BOOST_LOG_TRIVIAL(trace) << "Server initialization ended";
}



void Server::run() {
    string source_ip;
    string source_port;

    server_running = true; // TODO should i count it as running or not... yea... he should wait for the others
    while (server_running) {
        BOOST_LOG_TRIVIAL(info) << "Waiting for client...";
        auto [message_complex, message_length, source_address] = receive_next_message();
        source_ip = inet_ntoa(source_address.sin_addr);
        source_port = be16toh(source_address.sin_port);

        try {
            message_validation(message_complex, message_length);
        } catch (const invalid_message& e) {
            cerr << format("[PCKG ERROR] Skipping invalid package from %1%:%2%. %3%") %source_ip %source_port %e.what() << endl;
            BOOST_LOG_TRIVIAL(info) << format("[PCKG ERROR] Skipping invalid package from %1%:%2%. %3%") %source_ip %source_port %e.what();
            continue;
        }

        if (message_complex.command == cp::file_add_request) {
            BOOST_LOG_TRIVIAL(info) << "Message received: " << message_complex;
            handler(&Server::handle_upload_request, this, std::ref(source_address),
                    be64toh(message_complex.message_seq), message_complex.data, be64toh(message_complex.param));
        } else {
            auto message_simple = (SimpleMessage*) &message_complex;
            BOOST_LOG_TRIVIAL(info) << "Message received: " << *message_simple;

            if (message_simple->command == cp::discover_request) {
                handle_discover_request(source_address, be64toh(message_simple->message_seq));
            } else if (message_simple->command == cp::files_list_request) {
                handler(&Server::handle_files_list_request, this, std::ref(source_address),
                        be64toh(message_simple->message_seq), message_simple->data);
            } else if (message_simple->command == cp::file_get_request) {
                handler(&Server::handle_file_request, this, std::ref(source_address),
                        be64toh(message_simple->message_seq), message_simple->data);
            } else if (message_simple->command == cp::file_remove_request) {
                handle_remove_request(message_simple->data);
            }
        }
    }
}

void Server::stop() {
    server_running = false;
   // close(recv_socket);
}

bool Server::no_threads_running() {
    return true;
    return running_threads == 0;
}

ostream& operator << (ostream &out, const Server &server) {
    out << "\nSERVER INFO:";
    out << "\nMCAST_ADDR = " << server.multicast_address;
    out << "\nCMD_PORT = " << server.cmd_port;
    out << "\nMAX_SPACE = " << server.max_available_space;
    out << "\nFOLDER = " << server.shared_folder;
    out << "\nTIMEOUT = " << server.timeout;

    return out;
}