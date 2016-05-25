目的：使用epoll实现一个Linux服务器，具备可扩展性，可以提供HTTP服务，自定义的数据包服务等。

改进计划: (nonblock socket):

- 单线程+EPOLLET; （Reactor模型）
- 单线程+对象池+EPOLLET+日志; 
- 多线程+对象池+EPOLLET+EPOLLONESHOT; 

CHANGELOG
```
2016年05月25日14:06:17
单线程+EPOLLET 基本功能完成，实现简单的EchoServer
问题：对象池的使用有问题，没有正确将用完的对象返回给对象池。
```
```
2016年05月25日15:09:36
单线程+EPOLLET 改进了线程池，能够正确获取/释放对象。
```
