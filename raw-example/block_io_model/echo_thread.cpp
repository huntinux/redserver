/*************************************************************************
	> File Name: echo_thread.cpp
	> Author: 
	> Mail: 
	> Created Time: Fri 08 Jul 2016 09:16:08 AM CST
 ************************************************************************/

#include <iostream>

#include <cstdio>
#include <cstring>
#include <cerrno>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>  // struct sockaddr_in
#include <netinet/tcp.h> // TCP_NODELAY 
#include <arpa/inet.h>   // inet_pton 
#include <netdb.h>       // getnameinfo
#include <unistd.h>
#include <fcntl.h>       // fcntl
#include <stdlib.h>      
#include <signal.h>
#include <wait.h>

#include <string>
#include <vector>
#include <memory>

static const int kInvalidSocket = -1;
static const size_t kBuffMaxLen = 1024;

void printf_message(int fd, const char* msg, size_t len)
{
    if(len && (len <= kBuffMaxLen)) {
        char buff[kBuffMaxLen+ 10 + 7 + 20 + 1 + 8 + 1];
        char *dest = buff;
        dest += sprintf(dest, "[%8d]", fd);         // 10
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
        printf("[%8d]%s:  (host=%s, port=%s)\n", fd, msg, hbuf, sbuf);
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


struct ThreadParam {
    void* obj_;    
    int connfd_;
    ThreadParam(void* p = nullptr, int fd = -1) : obj_(p), connfd_(fd) {};
};
static void* DoWork(void* arg);
using std::string;

class ServerBase 
{
public:
    ServerBase(short port = 8998, const string &address = string("0.0.0.0")) 
        : listenfd_(kInvalidSocket), address_(address), port_(port) { }

    virtual ~ServerBase() {
        if(listenfd_ != kInvalidSocket) {
            close(listenfd_);
        }
    }

    int Read(int sfd, char* recv_buff, size_t len)
    {
        int n = recv(sfd, recv_buff, len, 0);
        if(n == -1) {
            if(errno == EINTR) {
                n = 0;
            } else {
                perror("recv");
                return -1;
            }
        } else if(n == 0) { /* client close the connection */
            printf("[%8d]Client close the connection.\n", sfd);
            return 0;
        }
        return n;
    }

    int Write(int sfd ,const char* send_buff, size_t len)
    {
        /* send the data back to client */
        int left = len, send_len = 0;
        while(left > 0) {
            int r = send(sfd, send_buff + send_len, len - send_len, 0);
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
        return send_len;
    }

    int Start() 
    {
        if((listenfd_ = socket(AF_INET, SOCK_STREAM, 0)) == kInvalidSocket) {
            perror("socket");
            return -1;
        }

        struct sockaddr_in sa;
        memset(&sa, 0, sizeof(sa));
        sa.sin_family = AF_INET;
        sa.sin_port = htons(port_);
        //sa.sin_addr.s_addr = INADDR_ANY;
        if(inet_pton(AF_INET, address_.c_str(), &sa.sin_addr.s_addr) != 1) {
            fprintf(stderr, "inet_pton maybe failed.\n");
            return -2;
        }

        if(make_socket_reusable(listenfd_) == -1) {
            printf("[%8d] Make socket reusable failed\n", listenfd_);
        }

        if(bind(listenfd_, (struct sockaddr*)&sa, sizeof(struct sockaddr_in)) == -1) {
            perror("bind");
            return -3;
        }

        if(listen(listenfd_, SOMAXCONN) == -1) {
            perror("listen ");
            return -4;
        }
        printf_address(listenfd_, (struct sockaddr*)&sa, sizeof(sa), "Listen on");

        while(1) {
            struct sockaddr_in sa_client;
            socklen_t sa_client_len = sizeof(sa_client);
            connfd_ = accept(listenfd_, (struct sockaddr*)&sa_client, &sa_client_len);
            if(connfd_ == -1) {
                perror("accept ");
                continue;
            }
            printf_address(connfd_, (struct sockaddr*)&sa_client, sa_client_len, "Accept");

            pthread_t tid;
            //auto tp = std::make_shared<ThreadParam>(this, connfd_); // 使用shared_ptr的想法是错误的，使用valgrind时发现该错误
            ThreadParam *tp = new ThreadParam(this, connfd_);
            if(0 != pthread_create(&tid, NULL, &DoWork, tp)) {
                printf("Pthread_create failed. Close the connection.\n");
                close(connfd_);
            }
        }
    }

    virtual void Work(int fd) { } 

    // No copying allowed 
    ServerBase(const ServerBase&);
    ServerBase& operator= (const ServerBase&);
   
protected:
    int listenfd_;
    std::string address_;
    short port_;
    int connfd_;
};


static void* DoWork(void* arg)
{
    //auto tp = static_cast<std::shared_ptr<ThreadParam>*>(arg);
    ThreadParam *tp = static_cast<ThreadParam*>(arg);
    ServerBase *srv = static_cast<ServerBase*>(tp->obj_);
    srv->Work(tp->connfd_);
    delete tp;
    return NULL;
}

class EchoServer : public ServerBase {
public:
    EchoServer(short port = 8998, const string &address = "0.0.0.0")
        : ServerBase(port, address) {}

    virtual void Work(int connfd) override
    {
        std::cout << "echo server"  << std::endl;
        while(1)
        {
            /* read data from the recfbuff of socket connfd sended by client */
            char buff[kBuffMaxLen]; 
            int n = 0;
            if((n = Read(connfd, buff, kBuffMaxLen)) <= 0) break;

            printf_message(connfd_, buff, n);

            /* send the data back to client */
            if(Write(connfd, buff, n) != n) break;
        }
        close(connfd);
    }
private:
};

int main()
{   
    /* Echo server */
    EchoServer es;
    es.Start();
    return 0;
}
