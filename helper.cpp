#include <boost/filesystem.hpp>

namespace fs = boost::filesystem;

using std::string;

// TODO wrong file handle...
size_t get_file_size(const string& file) {
    fs::path file_path {file};
    return fs::file_size(file_path);
}