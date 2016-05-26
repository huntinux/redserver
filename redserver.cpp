/**
 * Bugs: 
 * 2016年05月25日14:05:43 没有正确返回对象池中的对象(已修正)
 */

#include "net.h"

/* Echo Server Example */
int main()
{
    EPoll epoll;
    ConnectionListener<EchoHandler> conn_listener(&epoll);
    conn_listener.Listen("9090");
    epoll.Start();
}

