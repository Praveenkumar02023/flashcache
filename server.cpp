// stdlib
#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
// system
#include <fcntl.h>
#include <poll.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/ip.h>
// C++
#include <vector>
#include "hashtable.h"
#include <cstddef>

#define container_of(ptr, T, member) \
    ((T *)((char *)ptr - offsetof(T, member)))

typedef std::vector<uint8_t> Buffer;

enum {
    TAG_NIL = 0,
    TAG_ERR = 1,
    TAG_STR = 2,
    TAG_INT = 3,
    TAG_DBL = 4,
    TAG_ARR = 5,
};

enum {
    ERR_UNKNOWN = 1,
    ERR_TOO_BIG = 2,
}; 

static struct {
    HMap db;
} g_data;


struct Entry {
    HNode node;
    std::string key;
    std::string val;
};

static bool entry_eq(HNode *lhs, HNode *rhs) {
    Entry *le = container_of(lhs, Entry, node);
    Entry *re = container_of(rhs, Entry, node);

    return le->key == re->key;
}

static uint64_t str_hash(const uint8_t *data, size_t len) {
    uint32_t h = 0x811C9DC5;

    for (size_t i = 0; i < len; i++) {
        h = (h + data[i]) * 0x01000193;
    }

    return h;
}

struct Conn{

    int fd = -1;

    bool want_read = false;
    bool want_write = false;
    bool want_close = false;

    std::vector<uint8_t> incoming;
    Buffer outgoing;

};

static void msg(const char *msg) {
    fprintf(stderr, "%s\n", msg);
}

static void die(const char *msg) {
    int err = errno;
    fprintf(stderr, "[%d] %s\n", err, msg);
    abort();
}

static void msg_errno(const char *msg) {
    fprintf(stderr, "[errno:%d] %s\n", errno, msg);
}

const size_t k_max_msg = 32 << 20;  // 32 MB
const size_t k_max_args = 200 * 1000;

static void fd_set_nb(int fd) {
    errno = 0;
    int flags = fcntl(fd, F_GETFL, 0);
    if (errno) {
        die("fcntl error");
        return;
    }

    flags |= O_NONBLOCK;

    errno = 0;
    (void)fcntl(fd, F_SETFL, flags);
    if (errno) {
        die("fcntl error");
    }
}

// append to the back
static void
buf_append(std::vector<uint8_t> &buf, const uint8_t *data, size_t len) {
    buf.insert(buf.end(), data, data + len);
}


static void buf_append_u8(Buffer &buf , uint8_t data){

    buf.push_back(data);

}

static void buf_append_u32(Buffer &buf , uint32_t data){

    buf.insert(buf.end(),(uint8_t*)&data,(uint8_t *)&data+4);

}


static void buf_append_i64(Buffer &buf , uint64_t data){

    buf.insert(buf.end(),(uint8_t*)&data,(uint8_t *)&data+8);

}


static void buf_append_dbl(Buffer &buf , double data){

    buf.insert(buf.end(),(uint8_t*)&data,(uint8_t *)&data+8);

}

static void out_nil(Buffer & buf){
    buf_append_u8(buf, TAG_NIL);
}

static void out_str(Buffer &out, const char *s, size_t size) {
    buf_append_u8(out, TAG_STR);
    buf_append_u32(out, (uint32_t)size);

    buf_append(
        out,
        (const uint8_t *)s,
        size
    );
}

static void out_int(Buffer &out, int64_t val) {
    buf_append_u8(out, TAG_INT);
    buf_append_i64(out, val);
}

static void out_err(Buffer &out, uint32_t code, const std::string &msg) {
    buf_append_u8(out, TAG_ERR);
    buf_append_u32(out, code);
    buf_append_u32(out, (uint32_t)msg.size());

    buf_append(
        out,
        (const uint8_t *)msg.data(),
        msg.size()
    );
}

static void out_arr(Buffer &out, uint32_t n) {
    buf_append_u8(out, TAG_ARR);
    buf_append_u32(out, n);
}

static void response_begin(Buffer &out, size_t *header) {
    *header = out.size();
    buf_append_u32(out, 0);
}

static size_t response_size(Buffer &out, size_t header) {
    return out.size() - header - 4;
}

static void response_end(Buffer &out, size_t header) {
    size_t msg_size = response_size(out, header);

    if (msg_size > k_max_msg) {
        out.resize(header + 4);
        out_err(out, ERR_TOO_BIG, "response is too big.");
        msg_size = response_size(out, header);
    }

    uint32_t len = (uint32_t)msg_size;
    memcpy(&out[header], &len, 4);
}

// remove from the front
static void buf_consume(std::vector<uint8_t> &buf, size_t n) {
    buf.erase(buf.begin(), buf.begin() + n);
}

// application callback when the listening socket is ready
static Conn *handle_accept(int fd) {

    struct sockaddr_in client_addr = {};
    socklen_t addrlen = sizeof(client_addr);
    int connfd = accept(fd, (struct sockaddr *)&client_addr, &addrlen);
    if (connfd < 0) {
        msg_errno("accept() error");
        return NULL;
    }
    uint32_t ip = client_addr.sin_addr.s_addr;
    fprintf(stderr, "new client from %u.%u.%u.%u:%u\n",
        ip & 255, (ip >> 8) & 255, (ip >> 16) & 255, ip >> 24,
        ntohs(client_addr.sin_port)
    );

    // set the new connection fd to nonblocking mode
    fd_set_nb(connfd);

    // create a `struct Conn`
    Conn *conn = new Conn();
    conn->fd = connfd;
    conn->want_read = true;
    return conn;
}

static bool read_u32(const uint8_t* &cur , const uint8_t* end , uint32_t & out){

    if(cur + 4 > end){
        return false;
    }

    memcpy(&out, cur, 4);
    cur += 4;

    return true;
}

static bool read_str(const uint8_t*  &cur , const uint8_t* end , size_t n , std::string& out){

    if(cur + n > end) return false;

    out.assign(cur,cur+n);

    cur += n;

    return true;
}

// GET command
static void do_get(std::vector<std::string> &cmd, Buffer &out) {
    Entry key;

    key.key.swap(cmd[1]);

    key.node.hcode = str_hash(
        (const uint8_t *)key.key.data(),
        key.key.size()
    );

    HNode *node = hm_lookup(&g_data.db, &key.node, entry_eq);

    if (!node) {
        return out_nil(out);
    }

    const std::string &val =
        container_of(node, Entry, node)->val;

    return out_str(out, val.data(), val.size());
}

// SET command
static void do_set(std::vector<std::string> &cmd, Buffer &out) {
    Entry key;

    key.key.swap(cmd[1]);

    key.node.hcode = str_hash(
        (const uint8_t *)key.key.data(),
        key.key.size()
    );

    HNode *node = hm_lookup(&g_data.db, &key.node, entry_eq);

    if (node) {
        container_of(node, Entry, node)->val.swap(cmd[2]);
    } else {
        Entry *ent = new Entry();

        ent->key.swap(key.key);
        ent->node.hcode = key.node.hcode;
        ent->val.swap(cmd[2]);

        hm_insert(&g_data.db, &ent->node);
    }

    out_nil(out);
}

// DELETE command
static void do_del(std::vector<std::string> &cmd, Buffer &out) {
    Entry key;

    key.key.swap(cmd[1]);

    key.node.hcode = str_hash(
        (const uint8_t *)key.key.data(),
        key.key.size()
    );

    HNode *node = hm_delete(&g_data.db, &key.node, entry_eq);

    if (node) {
        delete container_of(node, Entry, node);
    }

    out_int(out, node ? 1 : 0);
}

// Command dispatcher
static void do_request(std::vector<std::string> &cmd, Buffer &out) {
    if (cmd.size() == 2 && cmd[0] == "get") {
        return do_get(cmd, out);
    }

    if (cmd.size() == 3 && cmd[0] == "set") {
        return do_set(cmd, out);
    }

    if (cmd.size() == 2 && cmd[0] == "del") {
        return do_del(cmd, out);
    }

    return out_err(out, ERR_UNKNOWN, "unknown command");
}


static int32_t parse_req(const uint8_t* data , size_t size , std::vector<std::string> &out){

    const uint8_t * end  = data + size;

    uint32_t nstr = 0;

    if(!(read_u32(data, end, nstr))){

        return -1;

    }
    
    if(nstr > k_max_args) return -1;

    while(out.size() < nstr){

        uint32_t len = 0;

        if(!read_u32(data,end,len)){
            return -1;
        }

        out.push_back(std::string());

        if(!read_str(data, end, len, out.back())){
            return -1;
        }
    }
    
    if(data != end){
        return -1;
    }

    return 0;
}

// process 1 request if there is enough data
static bool try_one_request(Conn *conn) {
    // try to parse the protocol
    if (conn->incoming.size() < 4) {
        return false;   
    }

    uint32_t len = 0;
    memcpy(&len, conn->incoming.data(), 4);

    if (len > k_max_msg) {
        msg("too long");
        conn->want_close = true;
        return false;   
    }

    // message body
    if (4 + len > conn->incoming.size()) {
        return false;   
    }
    
    const uint8_t *request = conn->incoming.data() + 4;
    
    //got one request 
    std::vector<std::string> cmd;

    if(parse_req(request,len,cmd) < 0){

        msg("bad request");
        conn->want_close = true;
        return false;

    }

   size_t header_pos = 0;

    response_begin(conn->outgoing, &header_pos);

    do_request(cmd, conn->outgoing);

    response_end(conn->outgoing, header_pos);
    
    //request done remove from request message.
    buf_consume(conn->incoming,  4+len);

    return true;    
}

// application callback when the socket is writable
static void handle_write(Conn *conn) {
    assert(conn->outgoing.size() > 0);
    ssize_t rv = write(conn->fd, &conn->outgoing[0], conn->outgoing.size());
    if (rv < 0 && errno == EAGAIN) {
        return;
    }
    if (rv < 0) {
        msg_errno("write() error");
        conn->want_close = true;    
        return;
    }

    // remove written data from `outgoing`
    buf_consume(conn->outgoing, (size_t)rv);

  
    if (conn->outgoing.size() == 0) {   
        conn->want_read = true;
        conn->want_write = false;
    } 
}

// application callback when the socket is readable
static void handle_read(Conn *conn) {
   
    uint8_t buf[64 * 1024];
    ssize_t rv = read(conn->fd, buf, sizeof(buf));
    if (rv < 0 && errno == EAGAIN) {
        return; 
    }
   
    if (rv < 0) {
        msg_errno("read() error");
        conn->want_close = true;
        return; 
    }
 
    if (rv == 0) {
        if (conn->incoming.size() == 0) {
            msg("client closed");
        } else {
            msg("unexpected EOF");
        }
        conn->want_close = true;
        return; 
    }
   
    buf_append(conn->incoming, buf, (size_t)rv);

    // parse requests and generate responses
    while (try_one_request(conn)) {}
   

   
    if (conn->outgoing.size() > 0) {   
        conn->want_read = false;
        conn->want_write = true;
        // The socket is likely ready to write in a request-response protocol,
        // try to write it without waiting for the next iteration.
        return handle_write(conn);
    }  
}


int main(){

   int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        die("socket()");
    }
    int val = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &val, sizeof(val));

    // bind
    struct sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(1234);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);    // wildcard address 0.0.0.0
    int rv = bind(fd, (const sockaddr *)&addr, sizeof(addr));
    if (rv) {
        die("bind()");
    }
        
    fd_set_nb(fd);

    rv = listen(fd, SOMAXCONN);
    if (rv) {
        die("listen()");
    }

    // a map of all client connections, keyed by fd
    std::vector<Conn *> fd2conn;
  
    std::vector<struct pollfd> poll_args;
    while (true) {
        // prepare the arguments of the poll()
        poll_args.clear();
        
        struct pollfd pfd = {fd, POLLIN, 0};
        poll_args.push_back(pfd);
 
        for (Conn *conn : fd2conn) {
            if (!conn) {
                continue;
            }
            // always poll() for error
            struct pollfd pfd = {conn->fd, POLLERR, 0};
            // poll() flags from the application's intent
            if (conn->want_read) {
                pfd.events |= POLLIN;
            }
            if (conn->want_write) {
                pfd.events |= POLLOUT;
            }
            poll_args.push_back(pfd);
        }

        // wait for readiness
        int rv = poll(poll_args.data(), (nfds_t)poll_args.size(), -1);
        if (rv < 0 && errno == EINTR) {
            continue;   
        }
        if (rv < 0) {
            die("poll");
        }

        // handle the listening socket
        if (poll_args[0].revents) {
            if (Conn *conn = handle_accept(fd)) {
                // put it into the map
                if (fd2conn.size() <= (size_t)conn->fd) {
                    fd2conn.resize(conn->fd + 1);
                }
                assert(!fd2conn[conn->fd]);
                fd2conn[conn->fd] = conn;
            }
        }

        // handle connection sockets
        for (size_t i = 1; i < poll_args.size(); ++i) { 
            uint32_t ready = poll_args[i].revents;
            if (ready == 0) {
                continue;
            }

            Conn *conn = fd2conn[poll_args[i].fd];
            if (ready & POLLIN) {
                assert(conn->want_read);
                handle_read(conn);  
            }
            if (ready & POLLOUT) {
                assert(conn->want_write);
                handle_write(conn);
            }

            // close the socket from socket error or application logic
            if ((ready & POLLERR) || conn->want_close) {
                (void)close(conn->fd);
                fd2conn[conn->fd] = NULL;
                delete conn;
            }
        }   
    }   
    return 0;
}