
阻塞I/O的例子。

多进程：
注意父子进程将共享文件描述符，它们需要把各自不需要的fd关闭掉。
父进程需要捕获SIGCHLD信号来处理僵尸进程

多线程：
注意传递给线程的参数会不会造成“竞争”,可以使用堆分配空间。使用shared_ptr不能解决该问题。