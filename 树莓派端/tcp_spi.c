/*************************************************
Function:tcp 服务端和客户端总接口，多线程实现一个服务端处理多个客户端通信服务
author:zyh
date:2020.4
**************************************************/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>//提供IP地址转换函数
#include <sys/types.h>//数据类型定义文件
#include <sys/socket.h>//提供socket函数及数结构
#include <netinet/in.h>//定义数据结构体sockaddr_in
#include <netinet/ip.h>
#include <pthread.h>
#include <stdbool.h>

/**
函数功能：tcp创建socket通信
入参：无
出参：无
返回：成功：socket通信句柄，失败：-1
**/
int tcp_creat_socket(void)
{
	int sockfd = 0;
	sockfd = socket(AF_INET, SOCK_STREAM, 0);
	if (0 > sockfd) {
		perror("socket");
		return -1;
	}
	
	//设置一下参数属性,防止服务端重启，出现地址占用问题
	int bReuseaddr = 1;//允许重用本地地址和端口, close socket（一般不会立即关闭而经历TIME_WAIT的过程）后想继续重用该socket
	if(0 > setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, (const char*)&bReuseaddr, sizeof(int))) {
		perror("setsockopt");
	}
	
#if 0
	int bDontLinger = 0;//如果要已经处于连接状态的soket在调用closesocket后强制关闭，不经历TIME_WAIT的过程：
	if(0 > setsockopt(sockfd, SOL_SOCKET, SO_LINGER, (const char*)&bDontLinger, sizeof(int))) {
		perror("setsockopt");
	}
#endif
	
#if 0
	// 接收缓冲区
	int nRecvBuf = 32*1024;//设置为32K
	setsockopt(sockfd, SOL_SOCKET, SO_RCVBUF, (const char*)&nRecvBuf, sizeof(int));
	//发送缓冲区
	int nSendBuf = 32*1024;//设置为32K
	setsockopt(sockfd, SOL_SOCKET, SO_SNDBUF, (const char*)&nSendBuf, sizeof(int));	
#endif

	return sockfd;
}

/**
函数功能：tcp客户端连接服务器
入参：socket：socket通信句柄
入参：server_ip：服务器ip
入参：server_port：服务器端口（提供给客户端连接的端口）
出参：无
返回：成功：0， 失败：-1
**/
int tcp_client_connect(int sockfd, char *server_ip, int server_port)
{
	unsigned int server_addr = 0;
	struct sockaddr_in st_server_addr = {0};

	st_server_addr.sin_family = AF_INET;
	st_server_addr.sin_port = htons(server_port);//端口号，无符号短整型数值转换为网络字节序，即大端模式(big-endian)
	inet_pton(AF_INET, server_ip, &server_addr);//ip转换函数，主机字节序转化为网络字节序
	st_server_addr.sin_addr.s_addr = server_addr;
	
	if (0 > connect(sockfd, (struct sockaddr *)&st_server_addr, sizeof(st_server_addr))) {
		perror("connect");
		return -1;
	}
	
	return 0;
}

/**
函数功能：tcp发送消息
入参：sockfd：句柄
入参：sendBuf：发送的消息内容；
入参：len：发送的消息内容长度（字节）(如果使用strlen计算长度时请注意碰到0x00会截至)
出参：无
返回：成功：实际发送的字节数，失败：-1
**/
int tcp_send(int sockfd,  void *sendBuf,  int len)
{
	int sendbytes = 0;

	if (0 > (sendbytes = send(sockfd, sendBuf, len,  MSG_DONTWAIT|MSG_NOSIGNAL))) {
		perror("send");
		return -1;
	}
	return sendbytes;
}

/**
函数功能：tcp堵塞接收消息
入参：sockfd：文件操作句柄
入参：recvBuf：接收的消息缓冲区（使用前后注意清空消息缓冲区，要不然存放消息会遗留上次接收的部分数据）
入参：len：缓冲区长度
出参：无
返回：成功：实际接收的字节数（其中：如果连接已中止，返回0）, 失败：-1
**/
int tcp_blocking_rcv(int sockfd, void *recvBuf, int len)
{
	int recvbytes = 0;

	if (0 > (recvbytes = recv(sockfd, recvBuf, len, 0)) ) {
		perror("recv");
		return -1;
	}
	
	return recvbytes;
}

/**
函数功能：tcp非堵塞接收消息
入参：sockfd：句柄
入参：recvBuf：接收的消息缓冲区（使用前后注意清空消息缓冲区，要不然存放消息会遗留上次接收的部分数据）
入参：len：缓冲区长度
入参：timeval_sec：超时时间(秒)
入参：timeval_usec：超时时间(微秒)
出参：无
返回：成功：实际接收的字节数（其中：如果连接已中止，返回0）, 失败：-1,  超时：-2
**/
int tcp_noblocking_rcv(int sockfd,  void *recvBuf, int len, int timeval_sec, int timeval_usec)
{
	fd_set readset;
	struct timeval timeout={0, 0};
	int maxfd = 0;
	int recvbytes = 0;
	int ret = 0;
	
	timeout.tv_sec = timeval_sec;
	timeout.tv_usec = timeval_usec;
	FD_ZERO(&readset);           
	FD_SET(sockfd, &readset);         

	maxfd = sockfd + 1;    

	ret = select(maxfd, &readset, NULL, NULL, &timeout); 
	if (0 > ret) {
		return -2;
	} else {
		if (FD_ISSET(sockfd, &readset)) {
			if (0 > (recvbytes = recv(sockfd, recvBuf, len, MSG_DONTWAIT))) {
				perror("recv");
				return -1;
			}
		} else {
			return -1;
		}
	}
	
	return recvbytes;
}

/**
函数功能：tcp关闭sockfd通信句柄
入参：sockfd：socket通信句柄
出参：无
返回：无
**/
void tcp_close(int sockfd)
{
	//close(sockfd);
	if (sockfd > 0) {  //sockfd等于0时不能关，防止把文件句柄0关掉，影响系统，会导致scanf()函数输入不了
		close(sockfd);
	}
}


/*********************以下是服务端多出来的部分********************************************************************************************/
/**
函数功能：tcp服务器绑定端口、监听设置
入参：sockfd：socket通信句柄
入参：server_ip：服务器本地IP
入参：server_port：服务器本地端口（提供给客户端连接的端口）
入参：max_listen_num:最大监听客户端的数目
出参：无
返回：成功返回0, 失败返回-1
**/
int tcp_server_bind_and_listen(int sockfd, char *server_ip, int server_port, int max_listen_num)
{
	unsigned int server_addr = 0;
	struct sockaddr_in st_LocalAddr = {0}; //本地地址信息结构图，下面有具体的属性赋值
	
	st_LocalAddr.sin_family = AF_INET;  //该属性表示接收本机或其他机器传输
	st_LocalAddr.sin_port = htons(server_port); //端口号，无符号短整型数值转换为网络字节序，即大端模式(big-endian)
	inet_pton(AF_INET, server_ip, &server_addr);//ip转换函数，主机字节序转化为网络字节序
	st_LocalAddr.sin_addr.s_addr = server_addr;
	
	//绑定地址结构体和socket
	if(0 > bind(sockfd, (struct sockaddr *)&st_LocalAddr, sizeof(st_LocalAddr))) {
		perror("bind");
		return -1;
	}
 
	//开启监听 ，第二个参数是最大监听数
	if(0 > listen(sockfd, max_listen_num)) {
		perror("listen");
		return -1;
	}
	
	return 0;
}

/**
函数功能：tcp阻塞等待客户端连接
入参：sockfd：socket通信句柄；
出参：无
返回：成功:与客户端连接后的新句柄，失败:-1
**/
int tcp_server_wait_connect(int sockfd)
{
	int new_sockfd = 0;//建立连接后的句柄

	struct sockaddr_in st_RemoteAddr = {0}; //对方地址信息
 	socklen_t socklen = 0;  
	
	//在这里阻塞直到接收到连接，参数分别是socket句柄，接收到的地址信息以及大小 
	new_sockfd = accept(sockfd, (struct sockaddr *)&st_RemoteAddr, &socklen);
	if(0 > new_sockfd) {
		perror("accept");
		return -1;
	}
	
	return new_sockfd;
}


/**
函数功能：服务端每接收到新的客户端连接,就创建新线程提供服务
入参：new_sockfd：客户端连接上服务端后产生的新socket句柄
入参：callBackFun:处理客户端消息的回调函数
出参：无
返回：无
**/
void tcp_server_creat_pthread_process_client(int *new_sockfd, void* (*callBackFun)(void*))
{
	pthread_t thread_id;
	int ret = 0;
	
	pthread_create(&thread_id, NULL, callBackFun, (void *)new_sockfd);
	pthread_detach(thread_id);//将线程分离, 线程结束后自动释放线程资源，后续不需要使用pthread_join()进行回收
}

//callBackFun:处理客户端消息的回调函数，示例
void *tcp_server_callBackFun_demo(void *ptr)
{
	//int new_sockfd = (int *)ptr;//错误，不能直接使用地址，防止外部地址数值改变
	int new_sockfd = *(int *)ptr;
	printf("新建线程处理客户端服务(new_sockfd=%d)\n", new_sockfd);
	
	char recv_buff[1024] = {0};
	int recv_len = 0;
	
	char *str = NULL;
	
	while (1) {
		memset(recv_buff, 0, sizeof(recv_buff));
		
		recv_len = tcp_blocking_rcv(new_sockfd, recv_buff, sizeof(recv_buff));//堵塞接收消息
		if(0 > recv_len) {
			printf("接收客户端消息失败(new_sockfd=%d)!\n", new_sockfd);
			break;
		} else if(0 == recv_len) {
			printf("客户端断开连接(new_sockfd=%d)\n", new_sockfd);
			break;
		} else {
			printf("接收客户端消息(new_sockfd=%d):%s\n", new_sockfd, recv_buff);
			str = (char *)"服务端已收到";
			tcp_send(new_sockfd, str, strlen(str));
		}
		//usleep(1*1000);
	}
	
	tcp_close(new_sockfd);
	
	printf("退出线程服务(new_sockfd=%d)\n", new_sockfd);
	return NULL;
}
