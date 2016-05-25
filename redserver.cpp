/**
 * Bugs: 
 * 2016年05月25日14:05:43 没有正确返回对象池中的对象
 */

#include <sys/epoll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <fcntl.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <sys/stat.h>
#include <sys/stat.h>
#include <netdb.h>
#include <netinet/in.h>
#include <net/if.h> 		
#include <string>
#include <iostream>
#include <typeinfo>
#include <vector>
#include <functional>
#include <memory>

#define TCP_NODELAY 0x0001
#define INVALID_SOCKET (-1)

static void printf_address(int fd, struct sockaddr *in_addr, socklen_t in_len, const char *msg = "")
{
	char hbuf[NI_MAXHOST], sbuf[NI_MAXSERV];
	if (getnameinfo(in_addr, in_len, hbuf, sizeof hbuf, sbuf, sizeof sbuf, NI_NUMERICHOST | NI_NUMERICSERV) == 0)
	{
		printf("[%8d] %s:  (host=%s, port=%s)\n", fd, msg, hbuf, sbuf);
	}
}
static bool make_socket_nonblock(int sfd)
{
	int flags;
	flags = fcntl(sfd, F_GETFL, 0);
	if (flags == -1)
	{
        perror("fcntl getfl");
		return false;
	}
	flags |= O_NONBLOCK;
	if (fcntl(sfd, F_SETFL, flags) == -1)
	{
        perror("fcntl setfl");
		return false;
	}
	flags = 1;
	if (setsockopt(sfd, IPPROTO_TCP, TCP_NODELAY, (char *)&flags, sizeof(int)) < 0)
	{
        perror("setcockopt TCP_NODELAY");
	}
	return true;
}

static int create_and_bind(const char *port, const char *address=NULL)
{
	struct addrinfo hints;
	struct addrinfo *result, *rp;
	int s;
	int sfd;

	memset(&hints, 0, sizeof (struct addrinfo));
	hints.ai_family = AF_UNSPEC;     /* Return IPv4 and IPv6 choices */
	hints.ai_socktype = SOCK_STREAM; /* We want a TCP socket */
	hints.ai_flags = AI_PASSIVE;     /* All interfaces */

	s = getaddrinfo(address, port, &hints, &result);
	if (s != 0)
	{
		return INVALID_SOCKET;
	}

	for (rp = result; rp != NULL; rp = rp->ai_next)
	{
		sfd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
		if (sfd == INVALID_SOCKET)
			continue;
		int enable = 1;
		if (setsockopt(sfd, SOL_SOCKET, SO_REUSEADDR, (char *)&enable, sizeof(int)) < 0)
		{
            perror("setcockopt SOCKET REUSEADDR");
		}

		s = bind(sfd, rp->ai_addr, rp->ai_addrlen);
		if (s == 0)
		{
			/* We managed to bind successfully! */
			printf_address(sfd, rp->ai_addr, rp->ai_addrlen, "Listen on");
			break;
		}
		close(sfd);
	}

	if (rp == NULL)
	{
		sfd = INVALID_SOCKET;
	}

	freeaddrinfo(result);

	return sfd;
}

/**
 * Simple Object Pool
 * 简单对象池的实现
 * 使用unique_ptr实现自动回收
 * 原文：http://www.csdn.net/article/2015-11-27/2826344-C++
 */
template<typename T>
class ObjectPoolAutoRelease
{
private:
    std::vector<std::unique_ptr<T>> pool;
public:
    using DeleteType = std::function<void(T*)>;

    ObjectPoolAutoRelease(size_t n = 0xFF)
    {
        allocNObjs(n);
    }

    void add(std::unique_ptr<T> t)
    {
        pool.push_back(std::move(t));
    }

    std::unique_ptr<T, DeleteType> getObject()
    {
        if(pool.empty())
        {
            //allocNObjs(0x10);
            throw std::logic_error("No more object");
        }
        std::unique_ptr<T, DeleteType> ptr(pool.back().release(), [this](T *t)
        {
            pool.push_back(std::unique_ptr<T>(t));
        });
        pool.pop_back();
        return ptr;
    }

    void releaseObject(T *obj)
    {
        pool.push(obj);
    }

    void allocNObjs(size_t n)
    {
        for(size_t i = 0; i < n; i++)
        {
            pool.push_back(std::unique_ptr<T>(new T));
        }
    }

    size_t count() const
    {
        return pool.size();
    }
};

/**
 * 简单的对象池
 * 用户手动放回对象
 * 默认首次分配255个对象，如果没有可用对象则抛出logic_error异常
 * 类型T必须有默认构造函数
 * @note 没有考虑线程安全和扩容等问题
 */
template<typename T>
class ObjectPool
{
private:
    std::vector<T*> pool;
public:
    ObjectPool(size_t n = 0xFF)
    {
        allocObjs(n);
    }

    ~ObjectPool()
    {
        printf("Release all objects.\n");
        while(!pool.empty())
        {
            auto o = pool.back();
            delete o;
            pool.pop_back();
        }
    }

    void allocObjs(size_t n)
    {
        for(size_t i = 0; i < n; i++) 
            pool.push_back(new T());
    }

    T* getObject()
    {
        if(pool.empty()) {
            throw std::logic_error("No more object");
        }
        auto o = pool.back();
        pool.pop_back();
        printf("[@@@@@@@@] Allocate obj %p ON POOL(%p) AVAILIABLE(%lu)\n", static_cast<void *>(o), this, pool.size());
        return o;
    }
    void releaseObject(T *o)
    {
        pool.push_back(o);
        printf("[@@@@@@@@] Release obj %p ON POOL(%p) AVAILIBLE(%lu)\n", static_cast<void *>(o), this, pool.size());
    }
    size_t count() const
    {
        return pool.size();
    }
};

/**
 * epoll 封装类的基类
 */
#define EPOLLMAXEVENT 64
class EPollBase
{
private:
    static const char *op_str[];
protected:
    int epoll_fd;
public:
    EPollBase() : epoll_fd(epoll_create1(0)){ }
    int Control(int op, int fd, struct epoll_event *event)
    {
#ifndef NDEBUG
        printf("[%8d] epoll_ctl op = %s [@Handler:%p]\n", fd, op_str[op], event->data.ptr);
        //printf("[%8d] epoll_ctl op = %s \n", fd, op_str[op]);
#endif
        int ret = epoll_ctl(epoll_fd, op, fd, event);
        if(ret == -1)
            perror("epoll_ctl");
        if(op == EPOLL_CTL_DEL) event->data.ptr = nullptr;
        return ret;
    }
};
const char* EPollBase::op_str[] = { "", "EPOLL_CTL_ADD", "EPOLL_CTL_DEL", "EPOLL_CTL_MOD"};

/**
 * 所有Handler的基类
 */
class EventHandlerBase
{
protected:
    EPollBase *epoll;
    int sfd; /* connfd or listenfd */
public:
    EventHandlerBase(EPollBase *ep = NULL) : epoll(ep), sfd(INVALID_SOCKET) {}
    virtual ~EventHandlerBase(){}
    virtual void HandleRead() 
    {
        printf("[%8d] Default HandleRead\n", sfd);
    }
    virtual void HandleWrite() 
    {
        printf("[%8d] Default HandleWrite\n", sfd);
    }
    virtual void HandleError() 
    {
        printf("[%8d] Default HandleError\n", sfd);
        if(sfd != INVALID_SOCKET)
        {
            printf("[%8d] Close connection and remove event on epoll\n", sfd);
            struct epoll_event e;
            epoll->Control(EPOLL_CTL_DEL, sfd, &e);
            close(sfd);
            sfd = INVALID_SOCKET;
        }
    }
};


class EPoll : public EPollBase
{
public:
    /**
     * 开始epoll_wait循环检测事件
     */
    bool Start()
    {
        struct epoll_event events[EPOLLMAXEVENT];

        while(true)
        {
            int n = epoll_wait(epoll_fd, events, EPOLLMAXEVENT, 1000);
            for(int i = 0; i < n; i++)
            {
                EventHandlerBase *ehb = static_cast<EventHandlerBase *>(events[i].data.ptr);
                if(events[i].events && EPOLLIN) 
                {
                    ehb->HandleRead();
                } 
                else if(events[i].events && EPOLLOUT) 
                {
                    ehb->HandleWrite();
                } 
                else // EPOLLERR EPOLLHUP
                { 
                    ehb->HandleError();
                }
            }
        }

    }
};


/**
 * 监听客户端连接的类，每个连接分配一个ConnectionHandler来处理
 */
template<typename TConnectionHandler>
class ConnectionListener : public EventHandlerBase
{
private:
    /**
     * 对象池：对象在ConnectionListener::HandleRead中分配，在ConnectionHandler::HandleError中释放。
     * TODO: 记得在ConnectionHandler处理连接断开时把资源放回对象池中。
     */
    ObjectPool<TConnectionHandler> objPool; 

public:
    ConnectionListener(EPollBase *ep) : EventHandlerBase(ep){}
    ~ConnectionListener()
    {
        if(sfd != INVALID_SOCKET)
            close(sfd);
    }
    bool Listen(const std::string &port, const std::string &address = "0.0.0.0")
    {
        sfd = create_and_bind(port.c_str(), address.c_str());
        if(sfd == INVALID_SOCKET)
            return false;

        if(!make_socket_nonblock(sfd))
            return false;

        if(listen(sfd, SOMAXCONN) == -1)
        {
            close(sfd);
            sfd = INVALID_SOCKET;
            return false;
        }

        struct epoll_event event;
        memset(&event, 0, sizeof(event));
        event.data.ptr = this;
        event.events = EPOLLIN | EPOLLET;

        return epoll->Control(EPOLL_CTL_ADD, sfd , &event) == 0;
    }

    /**
     * 处理客户端连接
     */
    void HandleRead()
    {
       while(true) 
       {
            struct sockaddr_in sa;
            socklen_t sa_len = sizeof(struct sockaddr_in);
            int connfd = accept(sfd, (struct sockaddr *)&sa, &sa_len);
            if(connfd == -1)
            {
                /* 接受了所有连接 */
                if(errno == EAGAIN || errno == EWOULDBLOCK)
                {
                    break;
                }
                else
                {
                    perror("accept");
                    break;
                }
            }

			printf_address(connfd, (struct sockaddr *)&sa, sa_len, "Accept");

            auto handler = objPool.getObject();
            handler->SetEPoll(epoll);
            handler->Process(connfd, sa, sa_len);
       }

    }

    void HandleError()
    {

    }

};

/**
 * 连接处理的基类
 */
class ConnectionHandler : public EventHandlerBase
{
public:
    ConnectionHandler(EPollBase *ep = NULL) : EventHandlerBase(ep){}

    /**
     * 可以执行自定义的清理操作
     * 如果使用了对象池，那么在使用对象前先清理一下
     */
    virtual void Init()
    { 
        printf("[%8d] Default Init\n", sfd); 
    }

    virtual void Exit()
    { 
        printf("[%8d] Default Exit\n", sfd); 
    }

    void SetEPoll(EPollBase *epb)
    {
        epoll = epb;
    }

    /**
     * ET模式下, EAGAIN或EWOULDBLOCK怎么处理
     */
	int Write(const void *buffer, int length)
	{
		const char *buf = (const char*)buffer;
		int total = length;

		while (length > 0)
		{
			int ret = send(sfd, buf, length, MSG_NOSIGNAL);
			if (ret < 0)
			{
				if (errno == EAGAIN || errno == EWOULDBLOCK)
				{
					struct epoll_event ev;
					ev.data.ptr = this;
					//ev.events = EPOLLOUT | EPOLLONESHOT | EPOLLET;
					ev.events = EPOLLOUT | EPOLLET;
					if(-1 == epoll->Control(EPOLL_CTL_MOD, sfd, &ev))
					{
						HandleError();
					}
					else
					{
						return total - length;
					}
				}
				else
				{
					HandleError();
				}
				return ret;
			}
			else
			{
				buf += ret;
				length -= ret;
			}
		}

		return total;
	}

    /**
     * ET模式下，循环读取，直到EAGAIN或EWOULDBLOCK
     * 返回读取的数据大小，发生错误时返回-1
     */
    int ReadAll(void *buff, int length)
    {
        int rn = 0;
        char *recvbuff = static_cast<char *>(buff);

        while(true)
        {
            int ret = recv(sfd, recvbuff, length, 0);
            if(ret < 0)
            {
                if(errno == EAGAIN || errno == EWOULDBLOCK)
                {
                    /*  read all data */ 
                    printf("[%8d] read %d: \"%s\"\n", sfd, rn, static_cast<char *>(buff));
                    break;
                }
                else
                {
                    /* other error */
                    perror("recv");
                    HandleError();
                    return -1;
                }
            }
            else if(ret == 0)
            {
                /* peer shutdown */
                HandleError();
                return -1;
            }
            else
            {
                rn += ret;
                recvbuff += ret;
                length -= ret;

                /* buff full */ 
                if(length <= 0)
                {
                    fprintf(stderr, "[%8d] ERROR: BUFF FULL. CLOSE THE CONNECTION\n", sfd);
                    HandleError();
                    return -1;
                }
            }

        }

        return rn;
    }

    /* For EPOLLET+EPOLLONESHOT */
	int Read(void *buffer, int length)
	{
		int ret;

        while(true) 
        {
		    ret = recv(sfd, (char*)buffer, length, 0);
		    if (ret <0)
		    {
		    	if (errno == EAGAIN || errno == EWOULDBLOCK)
		    	{
		    		struct epoll_event ev;
		    		ev.data.ptr = this;
		    		ev.events = EPOLLIN | EPOLLONESHOT | EPOLLET;
		    		if (-1 == epoll->Control(EPOLL_CTL_MOD, sfd, &ev))
		    		{
		    			HandleError();
		    		}
                    break;
		    	}
		    	else
		    	{
		    		HandleError();
		    	}
		    }
		    else if (ret == 0)
		    {
		    	HandleError();
		    }
        }
		return ret;
	}
    void Process(int connfd, struct sockaddr_in &sa, socklen_t sa_len)
    {
        this->sfd= connfd;
        this->sa = sa;
        this->sa_len = sa_len;

        Init();
        
        if(make_socket_nonblock(sfd))
        {
            struct epoll_event event;
            memset(&event, 0, sizeof(event));
            event.data.ptr = this;
            event.events = EPOLLIN | EPOLLET;

            if(epoll->Control(EPOLL_CTL_ADD, sfd, &event) == -1)
            {
		        fprintf(stderr, "[%8d] In ConnectionHandler epoll->Control failed\n", sfd);
                HandleError();
            }
        }
        else
        {
            HandleError();
        }
    }

    void HandleError() override
    {
        Exit();
        EventHandlerBase::HandleError();
        /* How to call objPool.releaseObject(this) ? Memory Leak Here */
        //ConnectionListener<ConnectionHandler>::objPool.releaseObject(this);
    }

private:
    struct sockaddr_in sa;
    socklen_t sa_len;
    std::string client_addr;
};

/**
 * 自定义的处理类
 */
class PackageHandler : public ConnectionHandler
{


};

#define BUFFMAX (1024 * 4)
class EchoHandler : public ConnectionHandler
{
private:
    char recvbuff[BUFFMAX];
    char sendbuff[BUFFMAX];
    int recvpos;
    int sendpos;
public:
    EchoHandler(EPollBase *ep = nullptr) : ConnectionHandler(ep)
    {
        recvpos = sendpos = 0;
    }
    ~EchoHandler(){}

    void Init() override
    {
        recvpos = sendpos = 0;
    }

    void HandleRead()
    {
        if((recvpos = ReadAll(recvbuff, BUFFMAX)) <= 0)
            return;
    
        strncpy(sendbuff, recvbuff, recvpos);

    	HandleWrite();
    }

    void HandleWrite()
    {
    	int needToSend = recvpos;
    	while (sendpos < needToSend)
    	{
    		int n = Write(sendbuff + sendpos, needToSend - sendpos);
    		if (n <= 0)  
    		{
    			return;
    		}
    		sendpos += n;
    	}
        sendpos = 0;
    }
};

/* Echo Server Example */
int main()
{
    EPoll epoll;
    ConnectionListener<EchoHandler> conn_listener(&epoll);
    conn_listener.Listen("9090");
    epoll.Start();
}

