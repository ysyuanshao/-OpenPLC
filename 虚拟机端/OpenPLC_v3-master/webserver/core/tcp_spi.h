/*************************************************
Function:tcp 服务端和客户端总接口，多线程实现一个服务端处理多个客户端通信服务
author:zyh
date:2020.4
**************************************************/

#ifndef _TCP_CLIENT_AND_SERVER_H_
#define _TCP_CLIENT_AND_SERVER_H_

int tcp_creat_socket(void);//创建socket
int tcp_client_connect(int sockfd, char *server_ip, int server_port);//tcp客户端连接服务器
int tcp_send(int sockfd,  void *sendBuf,  int len);//tcp发送消息
int tcp_blocking_rcv(int sockfd, void *recvBuf, int len);//tcp堵塞接收消息
int tcp_noblocking_rcv(int sockfd,  void *recvBuf, int len, int timeval_sec, int timeval_usec);//tcp非堵塞接收消息
void tcp_close(int sockfd);//tcp关闭socket通信

//服务端多出来的部分
int tcp_server_bind_and_listen(int sockfd, char *server_ip, int server_port, int max_listen_num);//tcp服务器绑定端口、监听设置
int tcp_server_wait_connect(int sockfd);//tcp阻塞等待客户端连接
void tcp_server_creat_pthread_process_client(int *new_sockfd, void* (*callBackFun)(void*));//服务端每接收到新的客户端连接,就创建新线程提供服务，外部需要重写处理消息的回调函数，参考void *tcp_server_callBackFun_demo(void *ptr)
void *tcp_server_callBackFun_demo(void *ptr);//callBackFun:处理客户端消息的回调函数，示例

#endif
