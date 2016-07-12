/*************************************************************************
	> File Name: epoll.c
	> Author:
	> Mail:
	> Created Time: Wed 06 Jul 2016 04:30:13 PM CST
 ************************************************************************/

#include <stdio.h>
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
#include <string>
#include <map>

#define INVALID_SOCKET (-1)
#define BUFFMAXLEN 1024
#define MAXEVENTS 64

/* A connection per buffer */
static const size_t kBuffMaxLen = 1024;
struct Connection {
    std::string recv_buff;
    size_t send_len;
    Connection() : recv_buff(""), send_len(0) {  }
};
/* Get connection buffer through fd */
std::map<int, Connection> conns;

/* */
std::string static_http_response;

void printf_message(int fd, const char* msg, size_t len)
{
    if(len && (len <= BUFFMAXLEN)) {
        char buff[BUFFMAXLEN + 15 + 7 + 20 + 1 + 8 + 1];
        char *dest = buff;
        dest += sprintf(dest, "[%8d(sfd)]", fd);    // 15
        dest += sprintf(dest, "Length:%lu ", len);  // 7 + 20 + 1
        dest += sprintf(dest, "Message:");          // 8
        size_t dlen = len;
        while(dlen--) {
            *dest++ = *msg++;
        }
        *dest = '\0'; // 1
        printf("%s\n", buff);
    }
}

void printf_address(int fd, struct sockaddr *in_addr, socklen_t in_len, const char *msg)
{
    char hbuf[NI_MAXHOST], sbuf[NI_MAXSERV];
    if (getnameinfo(in_addr, in_len,
                    hbuf, sizeof hbuf,
                    sbuf, sizeof sbuf,
                    NI_NUMERICHOST | NI_NUMERICSERV) == 0)
    {
        printf("[%8d(sfd)]%s:  (host=%s, port=%s)\n", fd, msg, hbuf, sbuf);
    }
}

int make_socket_non_blocking(int sfd)
{
    int flags;

    flags = fcntl(sfd, F_GETFL, 0);
    if (flags == -1)
    {
        perror("fcntl");
        return -1;
    }

    flags |= O_NONBLOCK;
    if (fcntl(sfd, F_SETFL, flags) == -1)
    {
        perror("fcntl");
        return -1;
    }
    flags = 1;
    if (setsockopt(sfd, IPPROTO_TCP, TCP_NODELAY, (char *)&flags, sizeof(int)) < 0)
    {
        printf("setsockopt TCP_NODELAY error, errno %d\n", errno);
    }
    return 0;
}

int make_socket_reusable(int sfd)
{
    int enable = 1;
    if (setsockopt(sfd, SOL_SOCKET, SO_REUSEADDR, (char *)&enable, sizeof(int)) < 0)
    {
        perror("setsockopt");
        return -1;
    }
    return 0;
}

int main()
{
    
    static_http_response = "HTTP/1.1 200 OK\r\nConnection: Keep-Alive\r\nContent-Type: text/html; charset=UTF-8\r\nContent-Length: 1048576\r\n\r\n123456";
    static_http_response.append(1048570, '\0');

    int listenfd = INVALID_SOCKET;

    if((listenfd = socket(AF_INET, SOCK_STREAM, 0)) == -1)
    {
        perror("socket");
        return -1;
    }

    struct sockaddr_in sa;
    memset(&sa, 0, sizeof(sa));
    sa.sin_family = AF_INET;
    sa.sin_port = htons(8998);
    sa.sin_addr.s_addr = INADDR_ANY;
    //if(inet_pton(AF_INET, "127.0.0.1", &sa.sin_addr.s_addr) != 1) {
    //    fprintf(stderr, "inet_pton maybe failed.\n");
    //    return -2;
    //}

    if(make_socket_reusable(listenfd) == -1)
    {
        printf("[%8d(sfd)] Make socket reusable failed\n", listenfd);
    }
    if(make_socket_non_blocking(listenfd) == -1)
    {
        printf("Make socket nonblock failed\n");
        return -1;
    }
    if(bind(listenfd, (struct sockaddr*)&sa, sizeof(struct sockaddr_in)) == -1)
    {
        perror("bind");
        return -3;
    }

    if(listen(listenfd, SOMAXCONN) == -1)
    {
        perror("listen ");
        return -4;
    }
    printf_address(listenfd, (struct sockaddr*)&sa, sizeof(sa), "Listen on");

    /* use epoll LT */
    int epfd = epoll_create1(0);
    if(epfd == -1) {
        perror("epoll_create1");
        return -5; 
    }

    struct epoll_event ev;
    ev.events = EPOLLIN;
    ev.data.fd = listenfd;

    if(-1 == epoll_ctl(epfd, EPOLL_CTL_ADD, listenfd, &ev)) {
        perror("epoll_ctl");
        return -1;
    }

    while(1) {
        struct epoll_event events[MAXEVENTS] ;
        int n = epoll_wait(epfd, events, MAXEVENTS, -1);
        for(int i = 0; i < n; i++) {
            uint32_t event = events[i].events;

            if(event & (EPOLLERR | EPOLLHUP)) {
                printf("EPOLLERR | EPOLLHUP\n");

                int fd = events[i].data.fd;
                close(fd);
                conns.erase(fd);

            } else if(event & EPOLLIN) {
                printf("EPOLLIN\n");
                if(events[i].data.fd == listenfd) {
                    //printf("listenfd\n");
                    /* listenfd*/
                    struct sockaddr_in sa_client;
                    socklen_t sa_client_len = sizeof(sa_client);
                    int connfd = accept(listenfd, (struct sockaddr*)&sa_client, &sa_client_len);
                    //if(connfd == -1) {
                    //    perror("accept ");
                    //    continue;
                    //}

                    if(make_socket_non_blocking(connfd) == -1) {
                        printf("Make socket nonblock failed\n");
                        close(connfd);
                        continue;
                    }
                    printf_address(connfd, (struct sockaddr*)&sa_client, sa_client_len, "Accept");

                    /* Register EPOLLIN on connfd */
                    struct epoll_event ev;
                    ev.events = EPOLLIN;
                    ev.data.fd= connfd;
                    if(-1 == epoll_ctl(epfd, EPOLL_CTL_ADD, connfd, &ev)) {
                        perror("epoll_ctl");
                        close(connfd);
                    }

                } else {

                    //printf("connfd\n");
                    /* connfd */
                    int connfd = events[i].data.fd;
                    Connection& conn = conns[connfd]; /* the [] of map will do insert when not exist */
                    char buff[2048];
                    
                    int n = recv(connfd, buff, sizeof buff, 0);
                    if(n == -1) {
                        if(errno == EAGAIN || errno == EWOULDBLOCK) {
                            continue;
                        }else if(errno == EINTR) {
                            n = 0;
                            continue;
                        } else {
                            perror("recv");
                            printf("[%8d]Error, Close the connection.\n", connfd);
                            close(connfd);
                            conns.erase(connfd);
                            continue;
                        }
                    } else if(n == 0) { /* client close the connection */
                        printf("[%8d]Client close the connection.\n", connfd);
                        close(connfd);
                        conns.erase(connfd); /* remove the buffer */
                        continue;
                    }
                    printf_message(connfd, buff, n);
                    conn.recv_buff.append(buff, n);

                    std::string &readed = conn.recv_buff;
                    if (readed.length()>4) {
                        if (readed.substr(readed.length()-2, 2) == "\n\n" || readed.substr(readed.length()-4, 4) == "\r\n\r\n") {
                            /* send response back to client */
                            /* Register EPOLLOUT */
                            struct epoll_event ev;
                            ev.events = EPOLLIN | EPOLLOUT;
                            ev.data.fd= connfd;
                            if(-1 == epoll_ctl(epfd, EPOLL_CTL_MOD, connfd, &ev)) {
                                perror("epoll_ctl");
                                close(connfd);
                                conns.erase(connfd);
                            }
                        }
                    }
                }
            } else if(event & EPOLLOUT) {
                //printf("EPOLLOUT\n");

                int connfd = events[i].data.fd;
                Connection& conn = conns[connfd]; /* the [] of map will do insert when not exist */
                int left = static_http_response.length() - conn.send_len;
                int error_occur = 0;
                while(left > 0) {
                    int n = send(connfd, static_http_response.c_str() + conn.send_len, left, 0);
                    if(n == -1) {
                        if(errno == EAGAIN || errno == EWOULDBLOCK) {
                            break;
                        } else if(errno == EINTR) {
                            continue;
                        } else {
                            perror("send");
                            error_occur = 1;
                            break;
                        }
                    }
                    left -= n;
                    conn.send_len += n;
                }
                printf("[%8d] Send: %lu, Left: %d\n", connfd, conn.send_len, left);
                
                /* send finished or error occurs, reset the client_data or free the client_data and  close the connfd */
                if(left == 0 || error_occur ) {
                    if(left == 0) {
                        printf("Send finised\n");
                        conn.send_len = 0;
                        /* Unregister EPOLLOUT */
                        struct epoll_event ev;
                        ev.events = EPOLLIN;
                        ev.data.fd = connfd;
                        if(-1 == epoll_ctl(epfd, EPOLL_CTL_MOD, connfd, &ev)) {
                            perror("epoll_ctl");
                            close(connfd);
                            conns.erase(connfd);
                        }
                    } else {
                        printf("Error occurs, close the connection.\n");
                        conns.erase(connfd);
                        close(connfd); /* when close there is a EPOLL_CTL_DEL automatically. */
                    }
                }

            } else {
                printf("Unknown event: 0x%X\n", event);
            }
    
        }
    }
}

