
#include <assert.h>
#include <cstddef>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/ip.h>
#include <vector>
#include <string>

static void msg(const char *msg) {
    fprintf(stderr, "%s\n", msg);
}

static void die(const char *msg) {
    int err = errno;
    fprintf(stderr, "[%d] %s\n", err, msg);
    abort();
}

static int32_t read_full(int fd, uint8_t *buf, size_t n) {
    while (n > 0) {
        ssize_t rv = read(fd, buf, n);

        if (rv < 0 && errno == EINTR) {
            continue;
        }

        if (rv <= 0) {
            return -1;
        }

        assert((size_t)rv <= n);
        n -= (size_t)rv;
        buf += rv;
    }

    return 0;
}

static int32_t write_all(int fd, const uint8_t *buf, size_t n) {
    while (n > 0) {
        ssize_t rv = write(fd, buf, n);

        if (rv < 0 && errno == EINTR) {
            continue;
        }

        if (rv <= 0) {
            return -1;
        }

        assert((size_t)rv <= n);
        n -= (size_t)rv;
        buf += rv;
    }

    return 0;
}

const size_t k_max_msg = 32 << 20;

static void buff_append(std::vector<uint8_t>& buff, const uint8_t *data, size_t len) {
    buff.insert(buff.end(), data, data + len);
}

// NEW: Encode and send a structured request
static int32_t send_req(int fd, const std::vector<std::string> &cmd) {

    uint32_t len = 4;

    for (const std::string &s : cmd) {
        len += 4 + s.size();
    }

    if (len > k_max_msg) {
        msg("message too long");
        return -1;
    }

    std::vector<uint8_t> wbuf(4 + len);

    // 4-byte message length
    memcpy(wbuf.data(), &len, 4);

    // Number of strings
    uint32_t n = (uint32_t)cmd.size();
    memcpy(wbuf.data() + 4, &n, 4);

    size_t cur = 8;

    // Encode each string
    for (const std::string &s : cmd) {

        uint32_t p = (uint32_t)s.size();

        memcpy(wbuf.data() + cur, &p, 4);
        memcpy(wbuf.data() + cur + 4, s.data(), s.size());

        cur += 4 + s.size();
    }

    return write_all(fd, wbuf.data(), wbuf.size());
}

// UPDATED: Read response length, status and data
static int32_t read_res(int fd) {

    std::vector<uint8_t> rbuf;

    rbuf.resize(4);

    errno = 0;

    int32_t err = read_full(fd, rbuf.data(), 4);

    if (err) {

        if (errno == 0) {
            msg("EOF");
        } else {
            msg("read() error");
        }

        return err;
    }

    uint32_t len = 0;

    memcpy(&len, rbuf.data(), 4);

    if (len > k_max_msg || len < 4) {
        msg("bad response length");
        return -1;
    }

    // Read response body
    rbuf.resize(4 + len);

    err = read_full(fd, rbuf.data() + 4, len);

    if (err) {
        msg("read() error");
        return err;
    }

    // Extract response status
    uint32_t rescode = 0;

    memcpy(&rescode, rbuf.data() + 4, 4);

    // Print response
    printf(
        "server says: [%u] %.*s\n",
        rescode,
        (int)(len - 4),
        (const char *)(rbuf.data() + 8)
    );

    return 0;
}

int main(int argc, char **argv) {

    int fd = socket(AF_INET, SOCK_STREAM, 0);

    if (fd < 0) {
        die("socket()");
    }

    struct sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(1234);
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

    int rv = connect(fd, (const struct sockaddr *)&addr, sizeof(addr));

    if (rv) {
        die("connect");
    }

    // Build command from command-line arguments
    std::vector<std::string> cmd;

    for (int i = 1; i < argc; ++i) {
        cmd.push_back(argv[i]);
    }

    int32_t err = send_req(fd, cmd);

    if (err) {
        goto L_DONE;
    }

    err = read_res(fd);

    if (err) {
        goto L_DONE;
    }

L_DONE:
    close(fd);
    return 0;
}
