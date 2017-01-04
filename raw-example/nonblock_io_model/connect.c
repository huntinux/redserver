/**
 * non-block connect with epoll 
 * 2017年01月04日14:37:19
 * jinger
 */

#include<stdio.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <string.h>
#include <netinet/in.h>  // struct sockaddr_in
#include <netinet/tcp.h> // TCP_NODELAY 
#include <arpa/inet.h>   // inet_pton 
#include <netdb.h>       // getnameinfo
#include <unistd.h>
#include <fcntl.h>       // fcntl
#include <errno.h>
#include <sys/epoll.h>   // epoll
#include <malloc.h>
#include <stdlib.h>      // atoi
#include <strings.h>     // bzero

#define MAXEVENTS   1024
#define MAXBUFF     1024

unsigned char sendbuff[MAXBUFF];
unsigned char recvbuff[MAXBUFF];
uint32_t sendlen = 0, recvlen = 0;
uint32_t nsend = 0, nread = 0;
int connected = 0;

void initbuff()
{
    bzero(sendbuff, sizeof(sendbuff));
    bzero(recvbuff, sizeof(recvbuff));
    strcpy(sendbuff, "helloworld");
    recvlen = sendlen = 10;
}

int main(int argc, char* argv[])
{   
    if(argc != 3) {
        printf("Usage: %s ip port", argv[0]);
        exit(EXIT_FAILURE);
    }

    initbuff();

    const char* sip = argv[1];      // server ip
    const char* sport = argv[2];    // server port
    struct sockaddr_in ssa;         // server sockaddr
    memset(&ssa, 0, sizeof(ssa));
    ssa.sin_family = AF_INET;
    ssa.sin_port = htons(atoi(sport));
    if(inet_pton(AF_INET, sip, &ssa.sin_addr.s_addr) != 1) {
        fprintf(stderr, "inet_pton failed.\n");
        exit(EXIT_FAILURE);
    }    

    // prepare epoll
    int epfd = epoll_create1(0);
    if(epfd == -1) {
        perror("epoll_create1");
        exit(EXIT_FAILURE);
    }


    // create socket non-block
    int fd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, IPPROTO_TCP);
    if(fd < 0) {
        fprintf(stderr, "socket failed.\n");
        exit(EXIT_FAILURE);
    }

    int r = connect(fd, (struct sockaddr*)&ssa, sizeof(ssa));
    if (r < 0 && errno != EINPROGRESS) {
        // error, fail somehow, close socket
        fprintf(stderr, "connect failed.\n");
        close(fd);
        exit(EXIT_FAILURE);
    }

    if (r == 0) {
        // connection has succeeded immediately
        printf("connection has succeeded immediatel.\n");
        connected = 1;
    } else {
        // connection attempt is in progress
        printf("connection attempt is in progress\n");
    }

    // watch EPOLLOUT 
    struct epoll_event ev;
    bzero(&ev, sizeof(ev));
    ev.events = EPOLLOUT;
    ev.data.fd = fd;
    if(-1 == epoll_ctl(epfd, EPOLL_CTL_ADD, fd, &ev)) {
        perror("epoll_ctl");
        exit(EXIT_FAILURE);
    }

    while(1) 
    {
        struct epoll_event events[MAXEVENTS];
        int n = epoll_wait(epfd, events, MAXEVENTS, 1000);
        for(int i = 0; i < n; i++) 
        {
            uint32_t event = events[i].events;
            if(event & EPOLLOUT) 
            {
                printf("EVENT: EPOLLOUT\n");
                if(!connected) 
                {
                    int result;
                    socklen_t result_len = sizeof(result);
                    if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &result, &result_len) < 0) 
                    {
                        // error, fail somehow, close socket
                        fprintf(stderr, "getsockopt failed.\n");
                        close(fd);
                        exit(EXIT_FAILURE);
                    }

                    if (result != 0) {
                        // connection failed; error code is in 'result'
                        printf("getsockopt result:%d\n", result);
                        close(fd);
                        exit(EXIT_FAILURE);
                    }
                }

                // socket is ready for write()
                //int n = send(fd, sendbuff + nsend, 1, 0); // send 1 byte one time
                int n = send(fd, sendbuff + nsend, sendlen - nsend, 0);
                if(n < 0) 
                {
                    if(errno == EAGAIN || errno == EWOULDBLOCK) {
                        printf("send EAGAIN or EWOULDBLOCK\n");
                        n = 0;
                    } else if(errno == EINTR) {
                        printf("EINTR\n");
                        n = 0;
                    } else {
                        perror("send:");
                        close(fd);
                        exit(EXIT_FAILURE);
                    }
                }
                nsend += n;

                if(nsend == sendlen) {
                    // stop watch EPOLLOUT, start watch EPOLLIN
                    printf("send finished. stop watch EPOLLOUT and start watch EPOLLIN.\n");
                    struct epoll_event ev;
                    ev.events = EPOLLIN;
                    ev.data.fd = fd;
                    if(-1 == epoll_ctl(epfd, EPOLL_CTL_MOD, fd, &ev)) {
                        perror("epoll_ctl");
                        close(fd);
                        exit(EXIT_FAILURE);
                    }
                }
            } else if(event & EPOLLIN) {
                printf("EVENT: EPOLLIN\n");

                //int n = recv(fd, recvbuff + nread, 1, 0); // recv 1 byte one time 
                int n = recv(fd, recvbuff + nread, sizeof(recvbuff) - nread, 0);
                if(n < 0) {
                    if(errno == EAGAIN || errno == EWOULDBLOCK) {
                        printf("recv EAGAIN or EWOULDBLOCK\n");
                        n = 0;
                    } else if(errno == EINTR) {
                        printf("EINTR\n");
                        n = 0;
                    } else {
                        perror("send:");
                        close(fd);
                        exit(EXIT_FAILURE);
                    }
                } else if(n == 0) {
                    printf("peer close the connection.\n");
                    close(fd);
                    exit(EXIT_FAILURE);
                }

                nread += n;
                if(nread == recvlen) {
                    printf("get enough data:%s\n", recvbuff);
                    exit(EXIT_SUCCESS);
                }
            } else if(event & (EPOLLERR | EPOLLHUP)) {
                printf("EVENT: EPOLLERR | EPOLLHUP\n");
                exit(EXIT_FAILURE);
            } else {
                printf("EVENT: OTHERS\n");
                exit(EXIT_FAILURE);
            }
        }
    }

    close(fd);
    close(epfd);
    return 0;
}

