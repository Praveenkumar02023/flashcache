#include <arpa/inet.h>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <netinet/in.h>
#include <sys/_endian.h>
#include <sys/_types/_socklen_t.h>
#include <sys/_types/_ssize_t.h>
#include <sys/socket.h>
#include <unistd.h>
#include <errno.h>

static void msg(const char *msg) {
    fprintf(stderr, "%s\n", msg);
}

static void die(const char *msg) {
    int err = errno;
    fprintf(stderr, "[%d] %s\n", err, msg);
    abort();
}

const size_t k_max_msg = 4096;

static int32_t read_full(int fd , char * buff , size_t n){

    while(n > 0){

        ssize_t rv = read(fd,buff,n);

        if(rv <= 0){
            return -1;
        }
        assert((size_t)rv <= n);

        n -= (size_t)rv;
        buff += rv;

    }

    return 0;
}

static int32_t write_full(int fd , char * buff , size_t n){

    while(n > 0){

        ssize_t rv = write(fd,buff,n);

        if(rv <= 0){
            return -1;
        }

        assert((size_t)rv <= n);

        n -= (size_t)rv;
        buff += rv;

    }

    return 0;
}

static int32_t one_request(int connfd)
{

    char rbuff[4 + k_max_msg];
    errno = 0;
    //read length of the body from request header since we are receving fixed prefix msgs
    int32_t err = read_full(connfd,rbuff,4);
    if(err){

        msg(errno == 0 ? "EOF"  : "Read() error");
        return err;

    }

    // get the len in integer 
    uint32_t len = 0;
    memcpy(&len, rbuff, 4);

    if(len > k_max_msg){
        msg("too long");
        return -1;
    }   
    //read complete request body
    err = read_full(connfd,&rbuff[4],len);
    if(err){

        msg("Read() error");
        return err;

    }
    //print client msg.
    fprintf(stderr, "client says: %.*s\n",len , &rbuff[4]);

    //reply
    const char reply[] = "hi from praveen server";
    char wbuff[4 + sizeof(reply)];
    len = (uint32_t)strlen(reply);
    memcpy(wbuff,&len,4);
    memcpy(&wbuff[4],reply,len);

    return write_full(connfd,wbuff,4 + len);    
}

int main(){

    // create a TCP socket
    int fd = socket(AF_INET,SOCK_STREAM,0);

    if(fd < 0){

       die("socket()");

    }

    //allow re-use of the same local address after restart.
    int reuse = 1;
    setsockopt(fd,SOL_SOCKET, SO_REUSEADDR , &reuse,  sizeof(reuse));

    // provide IP & Port address 

    sockaddr_in  addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(1234);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    
    if(bind(fd,(sockaddr*)&addr , sizeof(addr)) < 0){

       die("bind()");

    }

    if(listen(fd,SOMAXCONN)  < 0){

        die("listen()");

    }

    printf("server is listening on port 1234 \n");

    while(true){

        sockaddr_in client_addr {};
        socklen_t addrlen = sizeof(client_addr);

        int connfd = accept(fd, (sockaddr*)&client_addr, &addrlen);

        if(connfd < 0){

            perror("Accept failed !! \n");
            continue;

        }
        
        printf("Connected accepted \n");

        while(true){

            int32_t err = one_request(connfd);
            if(err){
                break;
            }

        }

        close(connfd);
        printf("Client disconnected\n");

    }

    return 0;
}