#include <arpa/inet.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <netinet/in.h>
#include <sys/_endian.h>
#include <sys/_types/_socklen_t.h>
#include <sys/_types/_ssize_t.h>
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
    
    if(bind(fd,(sockaddr*)&addr , sizeof(addr)) < 0){

        perror("bind");
        close(fd);
        return 1;

    }

    if(listen(fd,SOMAXCONN)  < 0){

        perror("listen");
        close(fd);
        return 1;

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

            char buffer[1024];

            ssize_t n = recv(connfd,buffer,sizeof(buffer),0);

            if(n <= 0){
                    //error occured or connection closed.
                break;
            }

            printf("Recevied! \n");

            ssize_t sent = send(connfd,buffer,n,0);

            if(sent <= 0){

                break;
            }

        }

        close(connfd);
        printf("Client disconnected\n");

    }

    return 0;
}