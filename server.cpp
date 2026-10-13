#include <arpa/inet.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <netinet/in.h>
#include <sys/_endian.h>
#include <sys/socket.h>
#include <unistd.h>

int main(){

    // create a TCP socket
    int fd = socket(AF_INET,SOCK_STREAM,0);

    if(fd < 0){

        perror("Socket failed!!");
        return 1;

    }

    //allow re-use of the same local address after restart.
    int reuse = 1;
    setsockopt(fd,SOL_SOCKET, SO_REUSEADDR , &reuse,  sizeof(reuse));

    // provide IP & Port address 

    sockaddr_in  addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(1234);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    



    return 0;
}