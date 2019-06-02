#ifndef DISTRIBUTED_FILES_STORAGE_HELPER_H
#define DISTRIBUTED_FILES_STORAGE_HELPER_H

#include <utility>
#include <thread>
#include <netinet/in.h>

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


/// Must be ended with '\0'
bool is_valid_string(const char* str, uint64_t length);

std::tuple<std::string, in_port_t> unpack_sockaddr(const struct sockaddr_in& address);

template<typename... A>
void handler(A &&... args) {
    std::thread handler{std::forward<A>(args)...};
    handler.detach();
}

template<typename T>
bool is_timeout(const std::chrono::time_point<T> &start_time, uint64_t timeout) {
    auto curr_time = std::chrono::steady_clock::now();
    std::chrono::duration<double, std::milli> elapsed_time = curr_time - start_time;
    return elapsed_time.count() / 1000 >= timeout;
}

template<typename T>
struct timeval get_elapsed_time_timeval(const std::chrono::time_point<T> &start_time, uint64_t total_time) {
    auto curr_time = std::chrono::steady_clock::now();
    auto microseconds_passed = std::chrono::duration_cast<std::chrono::microseconds>(curr_time - start_time).count();

    struct timeval timeval{};
    timeval.tv_sec = total_time - (ceil(microseconds_passed / 1e6));
    timeval.tv_usec = 1e6 - (microseconds_passed % (uint64_t)1e6);

    return timeval;
}

class invalid_user_input : public std::exception {
    const std::string message;
public:
    explicit invalid_user_input(std::string message = "Invalid message");

    const char* what() const noexcept override;
};


#endif //DISTRIBUTED_FILES_STORAGE_HELPER_H
