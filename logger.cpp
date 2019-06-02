#include <iostream>
#include <boost/format.hpp>
#include <boost/log/trivial.hpp>
#include <string>
#include "logger.h"

using std::cout;
using std::cerr;
using std::endl;
using boost::format;

void logger::server_discovery(const char* server_ip, const char* server_mcast_addr, uint64_t server_space) {
    cout << format("Found %1% (%2%) with free space %3%") %server_ip %server_mcast_addr %server_space << endl;
}

void logger::display_files_list(const std::vector<std::string>& files, const std::string& source_ip) {
    for (auto file: files)
        cout << format("%1% (%2%)") %file %source_ip << endl;
}

void logger::file_fetch_success(const std::string& filename, const std::string& server_ip, uint16_t server_port) {
    cout << format("File %1% downloaded (%2%:%3%)") %filename %server_ip %server_port << endl;
    BOOST_LOG_TRIVIAL(info) << format("File %1% downloaded (%2%:%3%)") %filename %server_ip %server_port;
}

void logger::file_fetch_failure(const std::string& filename, const std::string& server_ip,  uint16_t server_port, const std::string& reason) {
    cout << format("File %1% downloading failed (%2%:%3%) %4%") %filename %server_ip %server_port %reason << endl;
    BOOST_LOG_TRIVIAL(info) << format("File %1% downloading failed (%2%:%3%) %4%") %filename %server_ip %server_port %reason;
}

void logger::file_too_big(const std::string& file) {
    cout << format("File %1% too big") %file << endl;
    BOOST_LOG_TRIVIAL(info) << format("File %1% too big") %file;
}

void logger::file_not_exist(const std::string& file) {
    cout << format("File %1% does not exist") %file << endl;
    BOOST_LOG_TRIVIAL(info) << format("File %1% does not exist") %file;
}

void logger::file_upload_success(const std::string& filename, const std::string& server_ip, uint16_t server_port) {
    cout << format("File %1% uploaded (%2%:%3%)") %filename %server_ip %server_port << endl;
    BOOST_LOG_TRIVIAL(info) << format("File %1% uploaded (%2%:%3%)") %filename %server_ip %server_port;
}

void logger::file_upload_failure(const std::string& filename, const std::string& server_ip, uint16_t server_port, const std::string& reason) {
    cout << format("File %1% uploading failed (%2%:%3%) %4%") %filename %server_ip %server_port %reason << endl;
    BOOST_LOG_TRIVIAL(info) << format("File %1% uploading failed (%2%:%3%) %4%") %filename %server_ip %server_port %reason;
}

void logger::package_skipping(const std::string& source_ip, uint16_t source_port, const std::string& reason) {
    cerr << format("[PCKG ERROR] Skipping invalid package from %1%:%2%. %3%") %source_ip %source_port %reason << endl;
    BOOST_LOG_TRIVIAL(info) << format("[PCKG ERROR] Skipping invalid package from %1%:%2%. %3%") %source_ip %source_port %reason;
}

void logger::message_cout(const std::string& message) {
    cout << message << endl;
}

// TODO comment out...
void logger::syserr(const std::string& msg) {
    int errno1 = errno;

    //std::cerr << boost::format("ERROR: %1% %2% %3%") %msg %errno %strerror(errno1) << std::endl;
    BOOST_LOG_TRIVIAL(error) << boost::format("ERROR: %1% %2% %3%") %msg %errno %strerror(errno1);
}


