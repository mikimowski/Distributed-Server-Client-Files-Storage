#include <utility>
#include <vector>
#include <thread>
#include <chrono>
#include <csignal>

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
#include <bits/signum.h>

#include "server.h"
#include "../utilities/helper.h"
#include "../utilities/logger.h"

using std::string;
using std::to_string;
using std::vector;
using std::tuple;
using std::ostream;
using boost::format;

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

void Server::init_sockets() {
    recv_socket.create_socket();
    recv_socket.join_multicast_group(multicast_address);
    recv_socket.set_reuse_address(); // TODO chcemy to?
    recv_socket.bind(htobe16(cmd_port));

    send_socket.create_socket();
    send_socket.set_reuse_address(); // TODO chcemy to?
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

void Server::try_send_message(const SimpleMessage& message, const struct sockaddr_in& destination_address, uint16_t length) {
    try {
        send_socket.send(message, destination_address, length);
        BOOST_LOG_TRIVIAL(info) << format("Message to %1%:%2%:\n%3%")
                %inet_ntoa(destination_address.sin_addr) %be16toh(destination_address.sin_port) %message;
    } catch(const socket_failure& e) {
        BOOST_LOG_TRIVIAL(error) << e.what();
    }
}

void Server::try_send_message(const ComplexMessage& message, const struct sockaddr_in& destination_address, uint16_t length) {
    try {
        send_socket.send(message, destination_address, length);
        BOOST_LOG_TRIVIAL(info) << format("Message to %1%:%2%:\n%3%")
                %inet_ntoa(destination_address.sin_addr) %be16toh(destination_address.sin_port) %message;
    } catch(const socket_failure& e) {
        BOOST_LOG_TRIVIAL(error) << e.what();
    }
}

/**************************************************** DISCOVER ********************************************************/

void Server::handle_discover_request(const struct sockaddr_in& destination_address, uint64_t message_seq) {
    struct ComplexMessage message {htobe64(message_seq), cp::discover_response,
                                   this->multicast_address.c_str(), htobe64(this->get_available_space())};
    try_send_message(message, destination_address, multicast_address.length());
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
        if (filename.find(pattern) != string::npos) {
            if (cp::max_simple_data_size > curr_data_len + filename.length()) {
                fill_message_with_filename(message, &curr_data_len, filename);
            } else {
                try_send_message(message, destination_address, curr_data_len);
                curr_data_len = 0;
                memset(&message.data, '\0', sizeof(message.data));
            }
        }
    }
    if (curr_data_len > 0)
        try_send_message(message, destination_address, curr_data_len);
    BOOST_LOG_TRIVIAL(trace) << format("Handled files list request, pattern = %1%") %pattern;
}

/************************************************** DOWNLOAD FILE *****************************************************/

void Server::handle_file_request(struct sockaddr_in destination_address, uint64_t message_seq, string filename) {
    BOOST_LOG_TRIVIAL(info) << format("Starting file request, filename = %1%") % filename;
    tcp_socket tcp_sock;

    try {
        tcp_sock.create_socket();
        tcp_sock.bind();
        tcp_sock.listen();

        ComplexMessage message{htobe64(message_seq), cp::file_get_response, filename.c_str(),
                               htobe64(be16toh(tcp_sock.get_port()))};
        BOOST_LOG_TRIVIAL(trace) << format("sending TCP port info to %1%:%2%\n%3%")
                                    %inet_ntoa(destination_address.sin_addr) %be16toh(destination_address.sin_port) %message;
        send_socket.send(message, destination_address, filename.length());
    } catch (const socket_failure &e) {
        logger::syserr(e.what());
        return;
    }

    send_file_via_tcp(tcp_sock, filename);
    BOOST_LOG_TRIVIAL(info) << format("Handled file request, filename = %1%") % filename;
}

void Server::send_file_via_tcp(tcp_socket& tcp_sock, const string& filename) {
    BOOST_LOG_TRIVIAL(trace) << format("Starting sending file, port = %1%, filename = %2%") %be16toh(tcp_sock.get_port()) %filename;
    int select_res;

    try {
        select_res = tcp_sock.select(this->timeout);
    } catch(const socket_failure& e) {
        BOOST_LOG_TRIVIAL(error) << e.what();
        return;
    }

    if (select_res == 0) {
        BOOST_LOG_TRIVIAL(info) << format("Timeout on port:%1% no message received") %be16toh(tcp_sock.get_port());
    } else { // Client is waiting
        tcp_socket sock;
        try {
            sock = tcp_sock.accept();
            sock.get_port();
        } catch(const socket_failure &e) {
            logger::syserr(e.what());
            return;
        }

        fs::path file_path{this->shared_folder + filename};
        std::ifstream file_stream{file_path.c_str(), std::ios::binary}; // RAII closes
        if (file_stream.is_open()) {
            char buffer[MAX_BUFFER_SIZE];
            ssize_t length;

            while (file_stream) {
                file_stream.read(buffer, MAX_BUFFER_SIZE);
                length = file_stream.gcount();

                try {
                    sock.write(buffer, length);
                } catch(const socket_failure &e) {
                    logger::syserr(e.what());
                    return;
                }
            }
            BOOST_LOG_TRIVIAL(info) << format("Sending file via tcp finished, port = %1%, filename = %2%")
                                    %be16toh(tcp_sock.get_port()) %filename;
        } else {
            BOOST_LOG_TRIVIAL(error)
                << format("File opening error, filename = %1%, port = %2%") % filename %be16toh(tcp_sock.get_port());
        }
    }
}


/******************************************************* UPLOAD *******************************************************/

void Server::handle_upload_request(struct sockaddr_in destination_address, uint64_t message_seq,
                           string filename, uint64_t file_size) {
    BOOST_LOG_TRIVIAL(info) << format("File upload request, filename = %1%, filesize = %2%") %filename %file_size;
    bool can_upload = false;
    string rejection_reason;

    if (!filename.empty() && filename.find("/") == string::npos) {
        if (!(can_upload = check_and_reserve_space(file_size))) {
            rejection_reason = "not enough space";
        } else if (!(can_upload = add_file_to_storage(filename))) {
            free_space(file_size);
            rejection_reason = "file already in storage";
        } else {
            BOOST_LOG_TRIVIAL(info)
                << format("Accepting file upload request, filename = %1%, filesize = %2%") % filename % file_size;

            tcp_socket tcp_sock;
            try {
                tcp_sock.create_socket();
                tcp_sock.bind();
                tcp_sock.listen();

                ComplexMessage message{htobe64(message_seq), cp::file_add_acceptance, filename.c_str(),
                                       htobe64(be16toh(tcp_sock.get_port()))};
                send_socket.send(message, destination_address); // Send info about tcp port
            } catch (const socket_failure &e) {
                logger::syserr(e.what());
                free_space(file_size);
                remove_file_from_storage(filename);
                return;
            }

            upload_file_via_tcp(tcp_sock, filename, file_size);
        }
    }

    if (!can_upload) {
        BOOST_LOG_TRIVIAL(info) << format("Rejecting file upload request, filename = %1%, filesize = %2%, %3%") %filename %file_size %rejection_reason;
        SimpleMessage message {htobe64(message_seq), cp::file_add_refusal, filename.c_str()};
        try_send_message(message, destination_address, filename.length());
    }
}

void Server::upload_file_via_tcp(tcp_socket& tcp_sock, const string& filename, uint64_t file_size) {
    BOOST_LOG_TRIVIAL(trace) << format("Uploading file via tcp, port:%1%") %be16toh(tcp_sock.get_port());
    int select_res = 0;
    bool failed = false;
    uint64_t to_upload = file_size;
    fs::path file_path(this->shared_folder + filename);

    try {
        select_res = tcp_sock.select(this->timeout);
    } catch(const socket_failure& e) {
        failed = true;
        BOOST_LOG_TRIVIAL(error) << e.what();
    }

    if (select_res == 0) {
        failed = true;
        BOOST_LOG_TRIVIAL(info) << format("Timeout on port:%1% no message received") %be16toh(tcp_sock.get_port());
    } else {
        tcp_socket sock;
        try {
            sock = tcp_sock.accept();
        } catch(const socket_failure &e) {
            failed = true;
            BOOST_LOG_TRIVIAL(error) << e.what();
        }

        fs::ofstream fstream(file_path, std::ofstream::binary);
        if (fstream.is_open()) {
            ssize_t read_len;
            char buffer[MAX_BUFFER_SIZE];

            try {
                while ((read_len = sock.read(buffer, std::min(sizeof(buffer), to_upload))) > 0) {
                    to_upload -= read_len;
                    fstream.write(buffer, read_len);
                }
            } catch(const socket_failure& e) {
                failed = true;
                BOOST_LOG_TRIVIAL(error) << e.what();
            }
        } else {
            failed = true;
            BOOST_LOG_TRIVIAL(error)
                << format("File opening error, filename = %1%, port = %2%") %filename %be16toh(tcp_sock.get_port());
        }
    }

    if (failed || to_upload != 0) {
        BOOST_LOG_TRIVIAL(info) << format("Uploading failed, filename = %1%, received = %2%, expected = %3%, removing file") %filename %(file_size - to_upload) %file_size;
        if (fs::exists(file_path))
            fs::remove(file_path);
        free_space(file_size);
        remove_file_from_storage(filename);
    } else {
        BOOST_LOG_TRIVIAL(trace) << format("Ending uploading file via tcp, port:%1%") %be16toh(tcp_sock.get_port());
    }
}


/***************************************************** REMOVE *********************************************************/

void Server::handle_remove_request(string filename) {
    if (remove_file_from_storage(filename) > 0) {
        BOOST_LOG_TRIVIAL(info) << format("Deleting file, filename = %1%") %filename;
        fs::path file_path = this->shared_folder + filename;

        try {
            free_space(fs::file_size(file_path));
            fs::remove(file_path);
        } catch(const boost::filesystem::filesystem_error& e) {
            BOOST_LOG_TRIVIAL(error) << format("File deletion error, filename = %1%, %2%") %filename %e.what();
        }
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
void Server::message_validation(const ComplexMessage& message, ssize_t message_length) {
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

Server::Server(string mcast_addr, in_port_t cmd_port, uint64_t max_available_space, string shared_folder_path, uint16_t timeout)
    : multicast_address(std::move(mcast_addr)),
    cmd_port(cmd_port),
    max_available_space(max_available_space),
    shared_folder(*shared_folder_path.rend() == '/' ?  move(shared_folder_path) :  move(shared_folder_path) + "/"),
    timeout(timeout)
    {}

Server::Server(const ServerConfiguration& server_configuration)
    : Server(server_configuration.mcast_addr, server_configuration.cmd_port, server_configuration.max_space,
            server_configuration.shared_folder, server_configuration.timeout)
    {}

void Server::init() {
    BOOST_LOG_TRIVIAL(trace) << "Starting server initialization...";
    if (shared_folder != "./" && shared_folder != "../")
        fs::create_directories(shared_folder);
    signal(SIGPIPE, SIG_IGN);
    generate_files_in_storage();
    init_sockets();
    BOOST_LOG_TRIVIAL(trace) << "Server initialization ended";
}

void Server::run() {
    string source_ip;
    in_port_t source_port;
    ComplexMessage message_complex;
    ssize_t message_length;
    struct sockaddr_in source_address {};

    while (true) {
        BOOST_LOG_TRIVIAL(info) << "Waiting for client...";
        
        try {
            std::tie(message_complex, message_length, source_address) = recv_socket.recvfrom_complex();
        } catch(const socket_failure& e) {
            BOOST_LOG_TRIVIAL(error) << e.what();
            continue;
        }

        source_ip = inet_ntoa(source_address.sin_addr);
        source_port = be16toh(source_address.sin_port);
        try {
            message_validation(message_complex, message_length);
        } catch(const invalid_message& e) {
            logger::package_skipping(source_ip, source_port, e.what());
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
                handler(&Server::handle_files_list_request, this, source_address,
                        be64toh(message_simple->message_seq), message_simple->data);
            } else if (message_simple->command == cp::file_get_request) {
                if (is_in_storage(message_simple->data)) {
                    handler(&Server::handle_file_request, this, source_address,
                            be64toh(message_simple->message_seq), message_simple->data);
                } else {
                    logger::package_skipping(inet_ntoa(source_address.sin_addr), be16toh(source_address.sin_port), "no such file in storage");
                }
            } else if (message_simple->command == cp::file_remove_request) {
                handle_remove_request(message_simple->data);
            }
        }
    }
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
