#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>

#include <iostream>
#include <sstream>
#include <vector>
#include <string>

const size_t k_max_msg = 32 << 20;

// TLV tags - must match server.cpp
enum {
    TAG_NIL = 0,
    TAG_ERR = 1,
    TAG_STR = 2,
    TAG_INT = 3,
    TAG_DBL = 4,
    TAG_ARR = 5,
};

static void die(const char *msg) {
    fprintf(stderr, "[%d] %s\n", errno, msg);
    exit(EXIT_FAILURE);
}

// Read exactly n bytes.
static int32_t read_full(int fd, uint8_t *buf, size_t n) {
    while (n > 0) {
        ssize_t rv = read(fd, buf, n);

        if (rv < 0 && errno == EINTR) {
            continue;
        }

        if (rv <= 0) {
            return -1;
        }

        n -= (size_t)rv;
        buf += rv;
    }

    return 0;
}

// Write exactly n bytes.
static int32_t write_all(int fd, const uint8_t *buf, size_t n) {
    while (n > 0) {
        ssize_t rv = write(fd, buf, n);

        if (rv < 0 && errno == EINTR) {
            continue;
        }

        if (rv <= 0) {
            return -1;
        }

        n -= (size_t)rv;
        buf += rv;
    }

    return 0;
}

// Encode and send a request.
static int32_t send_req(
    int fd,
    const std::vector<std::string> &cmd
) {
    if (cmd.empty()) {
        return -1;
    }

    uint64_t len = 4;

    for (const std::string &s : cmd) {
        len += 4 + s.size();
    }

    if (len > k_max_msg || len > UINT32_MAX) {
        fprintf(stderr, "Message too long\n");
        return -1;
    }

    uint32_t msg_len = (uint32_t)len;
    std::vector<uint8_t> wbuf(4 + msg_len);

    // Message length.
    memcpy(wbuf.data(), &msg_len, 4);

    // Number of arguments.
    uint32_t n = (uint32_t)cmd.size();
    memcpy(wbuf.data() + 4, &n, 4);

    size_t cur = 8;

    // Encode every argument.
    for (const std::string &s : cmd) {
        uint32_t str_len = (uint32_t)s.size();

        memcpy(wbuf.data() + cur, &str_len, 4);
        cur += 4;

        memcpy(wbuf.data() + cur, s.data(), s.size());
        cur += s.size();
    }

    return write_all(fd, wbuf.data(), wbuf.size());
}

// Read a uint32 value.
static bool read_u32(
    const uint8_t *&cur,
    const uint8_t *end,
    uint32_t &out
) {
    if ((size_t)(end - cur) < 4) {
        return false;
    }

    memcpy(&out, cur, 4);
    cur += 4;

    return true;
}

// Read an int64 value.
static bool read_i64(
    const uint8_t *&cur,
    const uint8_t *end,
    int64_t &out
) {
    if ((size_t)(end - cur) < 8) {
        return false;
    }

    memcpy(&out, cur, 8);
    cur += 8;

    return true;
}

// Read a double value.
static bool read_dbl(
    const uint8_t *&cur,
    const uint8_t *end,
    double &out
) {
    if ((size_t)(end - cur) < 8) {
        return false;
    }

    memcpy(&out, cur, 8);
    cur += 8;

    return true;
}

// Decode and print one serialized value.
static bool print_response(
    const uint8_t *&cur,
    const uint8_t *end
) {
    if (cur >= end) {
        return false;
    }

    // First byte tells us the data type.
    uint8_t tag = *cur++;

    switch (tag) {

        case TAG_NIL: {
            std::cout << "OK\n";
            return true;
        }

        case TAG_ERR: {
            uint32_t code = 0;
            uint32_t len = 0;

            if (!read_u32(cur, end, code) ||
                !read_u32(cur, end, len)) {
                return false;
            }

            if ((size_t)(end - cur) < len) {
                return false;
            }

            std::string msg((const char *)cur, len);
            cur += len;

            std::cout << "Error [" << code << "]: "
                      << msg << "\n";

            return true;
        }

        case TAG_STR: {
            uint32_t len = 0;

            if (!read_u32(cur, end, len)) {
                return false;
            }

            if ((size_t)(end - cur) < len) {
                return false;
            }

            std::string val((const char *)cur, len);
            cur += len;

            std::cout << val << "\n";

            return true;
        }

        case TAG_INT: {
            int64_t val = 0;

            if (!read_i64(cur, end, val)) {
                return false;
            }

            std::cout << val << "\n";

            return true;
        }

        case TAG_DBL: {
            double val = 0;

            if (!read_dbl(cur, end, val)) {
                return false;
            }

            std::cout << val << "\n";

            return true;
        }

        case TAG_ARR: {
            uint32_t n = 0;

            if (!read_u32(cur, end, n)) {
                return false;
            }

            std::cout << "[\n";

            for (uint32_t i = 0; i < n; i++) {
                std::cout << "  ";

                if (!print_response(cur, end)) {
                    return false;
                }
            }

            std::cout << "]\n";

            return true;
        }

        default:
            fprintf(stderr, "Unknown TLV tag: %u\n", tag);
            return false;
    }
}

// Receive and decode a response.
static int32_t read_res(int fd) {
    uint32_t len = 0;

    if (read_full(fd, (uint8_t *)&len, 4) < 0) {
        fprintf(stderr, "Failed to read response length\n");
        return -1;
    }

    // TLV body can be as small as one byte (TAG_NIL).
    if (len == 0 || len > k_max_msg) {
        fprintf(stderr, "Invalid response length: %u\n", len);
        return -1;
    }

    std::vector<uint8_t> rbuf(len);

    if (read_full(fd, rbuf.data(), len) < 0) {
        fprintf(stderr, "Failed to read response body\n");
        return -1;
    }

    const uint8_t *cur = rbuf.data();
    const uint8_t *end = cur + rbuf.size();

    if (!print_response(cur, end) || cur != end) {
        fprintf(stderr, "Malformed response\n");
        return -1;
    }

    return 0;
}

// Convert user input into command arguments.
static std::vector<std::string> parse_command(
    const std::string &line
) {
    std::istringstream iss(line);

    std::vector<std::string> cmd;
    std::string arg;

    while (iss >> arg) {
        cmd.push_back(arg);
    }

    return cmd;
}

int main() {
    // Create TCP socket.
    int fd = socket(AF_INET, SOCK_STREAM, 0);

    if (fd < 0) {
        die("socket()");
    }

    // Server address.
    struct sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(1234);
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

    // Connect to server.
    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        die("connect()");
    }

    std::cout << "Connected to FlashCache!\n";
    std::cout << "Commands: set, get, del\n";
    std::cout << "Type exit to quit.\n\n";

    std::string line;

    while (true) {
        std::cout << "> ";

        if (!std::getline(std::cin, line)) {
            break;
        }

        std::vector<std::string> cmd = parse_command(line);

        if (cmd.empty()) {
            continue;
        }

        if (cmd[0] == "exit") {
            break;
        }

        // Send request.
        if (send_req(fd, cmd) < 0) {
            fprintf(stderr, "Failed to send request\n");
            break;
        }

        // Receive response.
        if (read_res(fd) < 0) {
            break;
        }
    }

    close(fd);

    std::cout << "Disconnected.\n";

    return 0;
}