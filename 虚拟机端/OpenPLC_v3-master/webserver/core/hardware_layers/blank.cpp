//-----------------------------------------------------------------------------
// Copyright 2015 Thiago Alves
//
// Based on the LDmicro software by Jonathan Westhues
// This file is part of the OpenPLC Software Stack.
//
// OpenPLC is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// OpenPLC is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with OpenPLC.  If not, see <http://www.gnu.org/licenses/>.
//------
//
// This file is the hardware layer for the OpenPLC. If you change the platform
// where it is running, you may only need to change this file. All the I/O
// related stuff is here. Basically it provides functions to read and write
// to the OpenPLC internal buffers in order to update I/O state.
// Thiago Alves, Dec 2015
//-----------------------------------------------------------------------------

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>
#include <string.h>
#include "ladder.h"
#include <netinet/in.h>
#include "custom_layer.h"
#include "../cJSON.h"
#include "../tcp_spi.h"
#define MAX_ROWS 1024
#define MAX_COLS 8
#define MAX_PI_IO 16
#define MAX_INPUT 14
#define MAX_OUTPUT 11
#define MAX_ANALOG_OUT 1
unsigned char log_msg[1000];
int sockfd = -1;				// 客户端socket通信句柄
int tcp_client_connectflag = 0; // 客户端socket通信连接标志
pthread_t thread_loop_data_holder, thread_client_connect_holder;

char server_ip[16] = "192.168.43.215"; // 服务器IP
int server_port = 8081;				   // 服务器端口
uint8_t pi_bool_input_buf[MAX_INPUT];
uint8_t pi_bool_output_buf[MAX_OUTPUT];
uint16_t pi_int_input_buf[MAX_ANALOG_OUT];
uint16_t pi_int_output_buf[MAX_ANALOG_OUT];

void *wirteDataToPi()
{
	char *json_str;

	if (tcp_client_connectflag == 0)
	{
		return -1;
	}
	// 往树莓派写数据
	sprintf(log_msg, "往树莓派写数据...\n");
	log(log_msg);
	int ret = 0;
	char recvBuf[1024 * 30] = {0};
	int recvBytes = 0;
	cJSON *root = cJSON_CreateObject();
	cJSON *matrix = cJSON_CreateArray();
	cJSON_AddStringToObject(root, "run_model", "W");
	cJSON_AddNumberToObject(root, "int_output", pi_int_output_buf[0]);
	pthread_mutex_lock(&bufferLock); // lock mutex
	for (int i = 0; i < MAX_OUTPUT; i++)
	{
		cJSON *item = cJSON_CreateNumber(pi_bool_output_buf[i]);
		cJSON_AddItemToArray(matrix, item);
	}
	cJSON_AddItemToObject(root, "bool_output", matrix);
	pthread_mutex_unlock(&bufferLock); // lock mutex
	json_str = cJSON_Print(root);
	printf("正在发送\n");
	if (0 > tcp_send(sockfd, json_str, strlen(json_str)))
	{ // 如果含有0x00不能用strlen
		printf("发送失败...！\n");
	}
	else
	{
		printf("发送成功\n");
	}
	// 堵塞接收
	memset(recvBuf, 0, sizeof(recvBuf));							// 清空
	recvBytes = tcp_blocking_rcv(sockfd, recvBuf, sizeof(recvBuf)); // 堵塞接收
	if (0 > recvBytes)
	{ // 接收失败
		sprintf(log_msg, "接收失败\n");
		log(log_msg);
	}
	else if (0 == recvBytes)
	{ // 断开了连接
		sprintf(log_msg, "已断开连接\n");
		log(log_msg);
	}
	else
	{
		sprintf(log_msg, "收到信息:%s\n", recvBuf);
		log(log_msg);
	}
}

void *readPiData()
{
	// 从树莓派远程获取针脚数据
	if (tcp_client_connectflag == 0)
	{
		sprintf(log_msg, "未连接服务器...!\n");
		log(log_msg);
		return -1;
	}
	sprintf(log_msg, "从树莓派远程获取针脚数据...!\n");
	log(log_msg);
	int ret = 0;
	char recvBuf[1024 * 30] = {0};
	int recvBytes = 0;
	char *json_str;
	cJSON *root = cJSON_CreateObject();
	cJSON_AddStringToObject(root, "run_model", "R");
	json_str = cJSON_Print(root);
	printf("正在发送\n");
	if (0 > tcp_send(sockfd, json_str, strlen(json_str)))
	{ // 如果含有0x00不能用strlen
		printf("发送失败...！\n");
		tcp_client_connectflag = 0;
	}
	else
	{
		printf("发送成功\n");
	}
	// 堵塞接收
	memset(recvBuf, 0, sizeof(recvBuf)); // 清空
	recvBytes = tcp_blocking_rcv(sockfd, recvBuf, sizeof(recvBuf));
	if (0 > recvBytes)
	{ // 接收失败
		sprintf(log_msg, "接收失败\n");
		log(log_msg);
	}
	else if (0 == recvBytes)
	{ // 断开了连接
		sprintf(log_msg, "已断开连接\n");
		log(log_msg);
	}
	else
	{
		printf("接收到消息:%s\n", recvBuf);
		sprintf(log_msg, "解析JSON\n");
		cJSON *root = cJSON_Parse(recvBuf);
		// 获取二维数组
		cJSON *matrix = cJSON_GetObjectItemCaseSensitive(root, "bool_input");
		// 假设我们知道 matrix 的大小或者我们可以动态确定它的大小
		const int rows = cJSON_GetArraySize(matrix); // 行数
		int cols = 0; // 列数，稍后确定
		int sparse[rows][3];
		pthread_mutex_lock(&bufferLock); // lock mutex
		for (int i = 0; i < rows; i++) {
			cJSON *row = cJSON_GetArrayItem(matrix, i);
			printf("index:%d,value:%d ", i,row->valueint);
			pi_bool_input_buf[i] = row->valueint;
		}
		pthread_mutex_unlock(&bufferLock); // lock mutex
		// 清理JSON对象
		cJSON_Delete(root);
	}
}
void *loop_query_and_send_data_from_pi(void *ptr)
{
	while (run_openplc)
	{
		readPiData();
		usleep(1000);
		wirteDataToPi();
		usleep(1000);
	}
}
void *fun_client_connect(void *ptr)
{
	sprintf(log_msg, "开始连接。。。。\n");
	log(log_msg);
	while (run_openplc)
	{
		if (0 == tcp_client_connectflag)
		{								 // 未连接就不断中断重连
			sockfd = tcp_creat_socket(); // 创建socket
			if (0 > sockfd)
			{
				sprintf(log_msg, "socket创建失败....!\n");
				log(log_msg);
				sleep(2);
				continue;
			}

			printf("请求连接...\n");

			if (0 > tcp_client_connect(sockfd, server_ip, server_port))
			{
				printf("连接失败...重连中...\n");
				sprintf(log_msg, "连接失败...重连中...\n");
				log(log_msg);
				sleep(2);
				continue;
			}
			else
			{
				tcp_client_connectflag = 1;
				sprintf(log_msg, "连接成功!\n");
				log(log_msg);
			}
		}
		else
		{
			sleep(1);
		}
	}

	tcp_close(sockfd);
}
//-----------------------------------------------------------------------------
// This function is called by the main OpenPLC routine when it is initializing.
// Hardware initialization procedures should be here.
//-----------------------------------------------------------------------------
void initializeHardware()
{
	int ret = 0;
	if (thread_client_connect_holder == NULL)
	{
		// 创建一个维持连接的线程
		ret = pthread_create(&thread_client_connect_holder, NULL, fun_client_connect, NULL);
		if (ret < 0)
		{
			printf("creat thread_client_rcv is fail!\n");
			return -1;
		}
	}

	if (thread_loop_data_holder == NULL)
	{
		// 创建一个轮询数据的线程
		ret = pthread_create(&thread_loop_data_holder, NULL, loop_query_and_send_data_from_pi, NULL);
		if (ret < 0)
		{
			printf("creat thread_client_rcv is fail!\n");
			return -1;
		}
	}
}

//-----------------------------------------------------------------------------
// This function is called by the main OpenPLC routine when it is finalizing.
// Resource clearing procedures should be here.
//-----------------------------------------------------------------------------
void finalizeHardware()
{
}

//-----------------------------------------------------------------------------
// This function is called by the OpenPLC in a loop. Here the internal buffers
// must be updated to reflect the actual Input state. The mutex bufferLock
// must be used to protect access to the buffers on a threaded environment.
//-----------------------------------------------------------------------------
void updateBuffersIn()
{
	pthread_mutex_lock(&bufferLock); // lock mutex
	printf("get lock  updateBuffersIn.........");
	for (int i = 0; i < MAX_INPUT; i++)
	{
		if (bool_input[i / 8][i % 8] != NULL)
			*bool_input[i / 8][i % 8] = pi_bool_input_buf[i];
	}
	pthread_mutex_unlock(&bufferLock); // unlock mutex
	printf("release lock updateBuffersIn.........");
}

//-----------------------------------------------------------------------------
// This function is called by the OpenPLC in a loop. Here the internal buffers
// must be updated to reflect the actual Output state. The mutex bufferLock
// must be used to protect access to the buffers on a threaded environment.
//-----------------------------------------------------------------------------
void updateBuffersOut()
{
	pthread_mutex_lock(&bufferLock); // lock mutex
	printf("get lock updateBuffersOut.........");
	for (int i = 0; i < MAX_OUTPUT; i++)
	{
		if (bool_output[i / 8][i % 8] != NULL)
			pi_bool_output_buf[i] = *bool_output[i / 8][i % 8];
	}
	if (int_output[0] != NULL)
	{
		pi_int_output_buf[0] = *int_output[0];
	}

	pthread_mutex_unlock(&bufferLock); // unlock mutex
	printf("release lock updateBuffersOut.........");
}
