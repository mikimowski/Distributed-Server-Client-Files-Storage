#include <boost/filesystem.hpp>
#include <sys/socket.h>
#include <chrono>
#include "err.h"

namespace fs = boost::filesystem;

using std::string;

void set_socket_timeout(int socket, uint16_t microseconds = 1000) {
    struct timeval timeval{};
    timeval.tv_usec = microseconds;

    if (setsockopt(socket, SOL_SOCKET, SO_RCVTIMEO, (void *) &timeval, sizeof(timeval)) < 0)
        syserr("setsockopt 'SO_RCVTIMEO'");
}


// TODO wrong file handle...
size_t get_file_size(const string& file) {
    fs::path file_path {file};
    return fs::file_size(file_path);
}