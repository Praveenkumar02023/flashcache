
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>

#include <iostream>
#include <sstream>
#include <string>
#include <vector>

static const size_t k_max_msg = 32 << 20;

typedef std::vector<uint8_t> Buffer;

// TLV response tags. These must match server.cpp.
enum {
    TAG_NIL = 0,
    TAG_ERR = 1,
    TAG_STR = 2,
    TAG_INT = 3,
    TAG_DBL = 4,
    TAG_ARR = 5,
};

// --------------------------------------------------
// Socket helpers
// --------------------------------------------------

static bool read_full(int fd, uint8_t *buf, size_t n) {
    while (n > 0) {
        ssize_t rv = read(fd, buf, n);

        if (rv == 0) {
            return false;
        }

        if (rv < 0) {
            if (errno == EINTR) {
                continue;
            }
            return false;
        }

        buf += rv;
        n -= (size_t)rv;
    }

    return true;
}

static bool write_all(int fd, const uint8_t *buf, size_t n) {
    while (n > 0) {
        ssize_t rv = write(fd, buf, n);

        if (rv < 0) {
            if (errno == EINTR) {
                continue;
            }
            return false;
        }

        if (rv == 0) {
            return false;
        }

        buf += rv;
        n -= (size_t)rv;
    }

    return true;
}

// --------------------------------------------------
// Request serialization
// --------------------------------------------------

static void buf_append(Buffer &buf, const uint8_t *data, size_t n) {
    buf.insert(buf.end(), data, data + n);
}

static void buf_append_u32(Buffer &buf, uint32_t value) {
    buf_append(buf, (const uint8_t *)&value, sizeof(value));
}

static bool send_req(int fd, const std::vector<std::string> &cmd) {
    Buffer payload;

    // Number of command arguments.
    buf_append_u32(payload, (uint32_t)cmd.size());

    // Serialize every argument as:
    // uint32 length + string bytes.
    for (const std::string &arg : cmd) {
        if (arg.size() > UINT32_MAX) {
            std::cerr << "Argument too large\n";
            return false;
        }

        buf_append_u32(payload, (uint32_t)arg.size());

        buf_append(
            payload,
            (const uint8_t *)arg.data(),
            arg.size()
        );
    }

    if (payload.size() > k_max_msg) {
        std::cerr << "Request too large\n";
        return false;
    }

    // TCP message framing:
    // uint32 payload length + payload.
    Buffer message;

    buf_append_u32(message, (uint32_t)payload.size());
    buf_append(message, payload.data(), payload.size());

    return write_all(fd, message.data(), message.size());
}

// --------------------------------------------------
// TLV deserialization helpers
// --------------------------------------------------

static bool read_u8(
    const uint8_t *&cur,
    const uint8_t *end,
    uint8_t &value
) {
    if (cur >= end) {
        return false;
    }

    value = *cur;
    cur++;

    return true;
}

static bool read_u32(
    const uint8_t *&cur,
    const uint8_t *end,
    uint32_t &value
) {
    if ((size_t)(end - cur) < sizeof(value)) {
        return false;
    }

    memcpy(&value, cur, sizeof(value));
    cur += sizeof(value);

    return true;
}

static bool read_i64(
    const uint8_t *&cur,
    const uint8_t *end,
    int64_t &value
) {
    if ((size_t)(end - cur) < sizeof(value)) {
        return false;
    }

    memcpy(&value, cur, sizeof(value));
    cur += sizeof(value);

    return true;
}

static bool read_dbl(
    const uint8_t *&cur,
    const uint8_t *end,
    double &value
) {
    if ((size_t)(end - cur) < sizeof(value)) {
        return false;
    }

    memcpy(&value, cur, sizeof(value));
    cur += sizeof(value);

    return true;
}

static bool read_string(
    const uint8_t *&cur,
    const uint8_t *end,
    uint32_t len,
    std::string &value
) {
    if ((size_t)(end - cur) < len) {
        return false;
    }

    value.assign((const char *)cur, len);
    cur += len;

    return true;
}

// --------------------------------------------------
// Recursive TLV response decoder
// --------------------------------------------------

static bool print_response(
    const uint8_t *&cur,
    const uint8_t *end
) {
    uint8_t tag = 0;

    if (!read_u8(cur, end, tag)) {
        return false;
    }

    switch (tag) {

    case TAG_NIL: {
        std::cout << "OK";
        return true;
    }

    case TAG_ERR: {
        uint32_t code = 0;
        uint32_t len = 0;
        std::string message;

        if (!read_u32(cur, end, code)) {
            return false;
        }

        if (!read_u32(cur, end, len)) {
            return false;
        }

        if (!read_string(cur, end, len, message)) {
            return false;
        }

        std::cout << "Error [" << code << "]: " << message;
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

        std::cout.write((const char *)cur, len);
        cur += len;

        return true;
    }

    case TAG_INT: {
        int64_t value = 0;

        if (!read_i64(cur, end, value)) {
            return false;
        }

        std::cout << value;
        return true;
    }

    case TAG_DBL: {
        double value = 0;

        if (!read_dbl(cur, end, value)) {
            return false;
        }

        std::cout << value;
        return true;
    }

    case TAG_ARR: {
        uint32_t count = 0;

        if (!read_u32(cur, end, count)) {
            return false;
        }

        // Basic sanity check: each element needs at least one tag byte.
        if ((size_t)(end - cur) < count) {
            return false;
        }

        std::cout << "[\n";

        for (uint32_t i = 0; i < count; i++) {
            std::cout << "  ";

            // IMPORTANT:
            // cur is passed by reference, so every nested value
            // advances the same cursor.
            if (!print_response(cur, end)) {
                return false;
            }

            std::cout << "\n";
        }

        std::cout << "]";
        return true;
    }

    default:
        std::cerr << "Unknown response tag: "
                  << (int)tag << "\n";
        return false;
    }
}

// --------------------------------------------------
// Response framing and decoding
// --------------------------------------------------

static bool read_res(int fd) {
    uint32_t len = 0;

    // Read the 4-byte response length.
    if (!read_full(fd, (uint8_t *)&len, sizeof(len))) {
        return false;
    }

    if (len == 0 || len > k_max_msg) {
        std::cerr << "Invalid response length: " << len << "\n";
        return false;
    }

    // Read the complete response body.
    Buffer body(len);

    if (!read_full(fd, body.data(), body.size())) {
        return false;
    }

    const uint8_t *cur = body.data();
    const uint8_t *end = cur + body.size();

    if (!print_response(cur, end)) {
        std::cerr << "\nMalformed response\n";
        return false;
    }

    // Ensure the decoder consumed the entire response.
    if (cur != end) {
        std::cerr << "\nMalformed response: "
                  << (end - cur)
                  << " trailing bytes\n";
        return false;
    }

    std::cout << "\n";
    return true;
}

// --------------------------------------------------
// Command parsing
// --------------------------------------------------

static std::vector<std::string> parse_command(const std::string &line) {
    std::vector<std::string> cmd;

    std::stringstream ss(line);
    std::string word;

    while (ss >> word) {
        cmd.push_back(word);
    }

    return cmd;
}

// --------------------------------------------------
// Main
// --------------------------------------------------

int main() {
    int fd = socket(AF_INET, SOCK_STREAM, 0);

    if (fd < 0) {
        perror("socket");
        return 1;
    }

    sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(1234);

    if (inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr) != 1) {
        std::cerr << "Invalid server address\n";
        close(fd);
        return 1;
    }

    if (connect(fd, (sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("connect");
        close(fd);
        return 1;
    }

    std::cout << "Connected to FlashCache!\n";
    std::cout << "Commands: set, get, del, keys\n";
    std::cout << "Type exit to quit.\n\n";

    std::string line;

    while (true) {
        std::cout << "> " << std::flush;

        if (!std::getline(std::cin, line)) {
            break;
        }

        if (line == "exit") {
            break;
        }

        std::vector<std::string> cmd = parse_command(line);

        if (cmd.empty()) {
            continue;
        }

        if (!send_req(fd, cmd)) {
            std::cerr << "Failed to send request\n";
            break;
        }

        if (!read_res(fd)) {
            std::cerr << "Disconnected.\n";
            break;
        }
    }

    close(fd);
    return 0;
}
