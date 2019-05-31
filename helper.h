#ifndef DISTRIBUTED_FILES_STORAGE_HELPER_H
#define DISTRIBUTED_FILES_STORAGE_HELPER_H

#include <utility>

#define MAX_BUFFER_SIZE 50000

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
bool is_valid_string(const char* str, uint64_t max_len);

bool is_valid_data(const char* data, uint64_t length);

/**
 * @return True if given pattern is a substring of given string,
 *         false otherwise
 */
bool is_substring(const std::string &pattern, const std::string &str);

void display_log_separator();

class invalid_message : public std::exception {
    const std::string message;
public:
    explicit invalid_message(std::string message = "Invalid message");

    const char* what() const noexcept override;
};

class invalid_user_input : public std::exception {
    const std::string message;
public:
    explicit invalid_user_input(std::string message = "Invalid message");

    const char* what() const noexcept override;
};

#endif //DISTRIBUTED_FILES_STORAGE_HELPER_H
