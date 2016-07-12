/*************************************************************************
	> File Name: epoll_et.c 
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

#define INVALID_SOCKET (-1)
#define BUFFMAXLEN 1024
#define MAXEVENTS 64

struct client_data {
    int fd;
    uint32_t recv_len;
    uint32_t send_len;
    char recv_buff[BUFFMAXLEN];
};

void printf_message(int fd, const char* msg, size_t len)
{
    if(len && (len <= BUFFMAXLEN)) {
        char buff[BUFFMAXLEN + 15 + 7 + 20 + 1 + 8 + 1];
        char *dest = buff;
        dest += sprintf(dest, "[%8d(sfd)]", fd);         // 15
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

void* do_echo(void *arg)
{
    int connfd = *(int*)(arg);
    while(1)
    {
        /* read data from the recfbuff of socket connfd sended by client */
        char buff[BUFFMAXLEN]; 
        int n = recv(connfd, buff, sizeof(buff), 0);
        if(n == -1) {
            if(errno == EINTR) {
                n = 0;
            } else {
                perror("recv");
                break;
            }
        } else if(n == 0) { /* client close the connection */
            break;
        }

        printf_message(connfd, buff, n);

        /* send the data back to client */
        int left = n, send_len = 0;
        while(left > 0) {
            int r = send(connfd, buff + send_len, n - send_len, 0);
            if(r == -1) {
                if(errno == EINTR) {
                    continue;
                } else {
                    break;
                }
            }
            left -= r;
            send_len += r;
        }
    }
    close(connfd);
    return NULL;
}

int main()
{
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
    ev.events = EPOLLIN | EPOLLET;
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
                /* delete epoll event on it, close it */ 
            } else if(event & EPOLLIN) {
                printf("EPOLLIN\n");
                if(events[i].data.fd == listenfd) {
                    printf("listenfd\n");
                    /* listenfd*/
                    while(1) {
                        struct sockaddr_in sa_client;
                        socklen_t sa_client_len = sizeof(sa_client);
                        int connfd = accept(listenfd, (struct sockaddr*)&sa_client, &sa_client_len);
                        if(connfd == -1) {
                            if(errno == EAGAIN || errno == EWOULDBLOCK) {
                                printf("Accept all connection.\n");
                                break;
                            } else {
                                perror("accept ");
                                break;
                            } 
                        }

                        if(make_socket_non_blocking(connfd) == -1) {
                            printf("Make socket nonblock failed\n");
                            close(connfd);
                            break;
                        }
                        printf_address(connfd, (struct sockaddr*)&sa_client, sa_client_len, "Accept");

                        /* allocate buffer */
                        struct client_data *cd = (struct client_data*)malloc(sizeof(struct client_data));
                        cd->fd = connfd;
                        cd->recv_len = cd->send_len = 0;
                        memset(cd->recv_buff, 0, sizeof(cd->recv_buff));

                        struct epoll_event ev;
                        ev.events = EPOLLIN | EPOLLET;
                        ev.data.ptr = cd;
                        if(-1 == epoll_ctl(epfd, EPOLL_CTL_ADD, connfd, &ev)) {
                            perror("epoll_ctl");
                            close(connfd);
                        }

                    }    
            
                } else {

                    printf("connfd\n");
                    /* connfd */
                    struct client_data *cd = (struct client_data*)events[i].data.ptr;
                    int connfd = cd->fd;
                    int closed = 0;
                    
                    /* read data from the recfbuff of socket connfd sended by client */
                    while(1) {
                        int n = recv(connfd, cd->recv_buff + cd->recv_len, sizeof(cd->recv_buff) - cd->recv_len, 0);
                        if(n == -1) {
                            if(errno == EAGAIN || errno == EWOULDBLOCK) {
                                /* read all data, BUG: if buff is not enough? */
                                break;
                            }else if(errno == EINTR) {
                                n = 0;
                                continue;
                            } else {
                                perror("recv");
                                printf("[%8d]Error, Close the connection.\n", connfd);
                                close(connfd);
                                free(cd);
                                break;
                            }
                        } else if(n == 0) { /* client close the connection */
                            printf("[%8d]Client close the connection.\n", connfd);
                            close(connfd);
                            free(cd);
                            closed = 1;
                            break;
                        }

                        printf_message(connfd, cd->recv_buff + cd->recv_len, n);
                        cd->recv_len += n;
                    }

                    /* send the data back to client */
                    /* Register EPOLLOUT */
                    if(!closed) {
                        struct epoll_event ev;
                        ev.events = EPOLLIN | EPOLLOUT | EPOLLET;
                        ev.data.ptr = cd;
                        if(-1 == epoll_ctl(epfd, EPOLL_CTL_MOD, connfd, &ev)) {
                            perror("epoll_ctl");
                            close(connfd);
                            free(cd);
                        }
                    }    
                }
            } else if(event & EPOLLOUT) {
                printf("EPOLLOUT\n");
                struct client_data *cd = (struct client_data*)events[i].data.ptr;
                int connfd = cd->fd;
                int left = cd->recv_len - cd->send_len;
                int error_occur = 0;
                while(left > 0) {
                    int n = send(connfd, cd->recv_buff + cd->send_len, left, 0);
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
                    cd->send_len += n;
                }
                
                /* send finished or error occurs, reset the client_data or free the client_data and  close the connfd */
                if(left == 0 || error_occur ) {
                    if(left == 0) {
                        printf("Send finised\n");
                        cd->recv_len = cd->send_len = 0;
                        /* Unregister EPOLLOUT */
                        //struct epoll_event ev;
                        //ev.events = EPOLLIN;
                        //ev.data.ptr = cd;
                        //if(-1 == epoll_ctl(epfd, EPOLL_CTL_MOD, connfd, &ev)) {
                        //    perror("epoll_ctl");
                        //    close(connfd);
                        //    free(cd);
                        //}
                    } else {
                        printf("Error occurs, close the connection.\n");
                        free(cd);
                        close(connfd); /* when close there is a EPOLL_CTL_DEL automatically. */
                    }
                }

            } else {
                printf("unknown event: 0x%X\n", event);
            }
    
        }
    }
}

