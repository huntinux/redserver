/*************************************************************************
	> File Name: One connection per thread
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
#include <pthread.h>
#include <stdlib.h>     // malloc

#define INVALID_SOCKET (-1)
#define BUFFMAXLEN 1024

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
            printf("[%8d(sfd)][%8lu(tid)]Client close the connection.\n", connfd, pthread_self());
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

        /* send the data back to client */
        //int send_len = 0;
        //while(1) {
        //    int r = send(connfd, buff + send_len, n - send_len, 0);
        //    if(r == -1) {
        //        if(errno == EINTR) {
        //            r = 0;
        //            continue;
        //        } else {
        //            perror("send");
        //            break;
        //        }
        //    } else if(r == 0) {
        //        break;   
        //    } else {
        //        printf("[%8d(sfd)][%8lu(tid)]Send %d data.\n", connfd, pthread_self(), r);
        //        send_len += r;
        //    }
        //}
    }
    close(connfd);
    printf("[%8d(sfd)][%8lu(tid)]Close socket.\n", connfd, pthread_self());
    free(arg);
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

    while(1)
    {
        struct sockaddr_in sa_client;
        socklen_t sa_client_len = sizeof(sa_client);
        int connfd = accept(listenfd, (struct sockaddr*)&sa_client, &sa_client_len);
        if(connfd == -1) {
            perror("accept ");
            continue;
        }

        printf_address(connfd, (struct sockaddr*)&sa_client, sa_client_len, "Accept");

        int *sfd = (int*)malloc(sizeof(int));
        *sfd = connfd;
        pthread_t tid;
        if(0 != pthread_create(&tid, NULL, do_echo, sfd)) { /* 传递给线程的参数是一个heap variable，不会发生竞争,但是要记得在线程中释放 */
            printf("Pthread_create failed. Close the connection.\n");
            close(connfd);
        }
    }

    close(listenfd);
    return 0;
}

