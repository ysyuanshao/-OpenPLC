/*************************************************
本服务器由此作者提供，在此基础上修改
Function:tcp 服务端进程，服务器中运行只一个
author:zyh
date:2020.4
**************************************************/
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdint.h>
#include <pthread.h>
#include <wiringPi.h>
#include <wiringSerial.h>
#include "tcp_spi.h"
#include "cJSON.h"
#define BUFFER_SIZE 1024
typedef uint8_t IEC_BOOL;
typedef uint16_t IEC_UINT;
int ignored_bool_inputs[] = {-1};
int ignored_bool_outputs[] = {-1};
int ignored_int_inputs[] = {-1};
int ignored_int_outputs[] = {-1};
IEC_UINT int_output[BUFFER_SIZE];
#if !defined(ARRAY_SIZE)
#define ARRAY_SIZE(x) (sizeof((x)) / sizeof((x)[0]))
#endif

#define MAX_INPUT 14
#define MAX_OUTPUT 11
#define MAX_ANALOG_OUT 1
#define MAX_ROWS 1024
#define MAX_COLS 8
#define MAX_PI_IO 16
/********************I/O PINS CONFIGURATION*********************
 * A good source for RaspberryPi I/O pins information is:
 * http://pinout.xyz
 *
 * The buffers below works as an internal mask, so that the
 * OpenPLC can access each pin sequentially
 ****************************************************************/
// inBufferPinMask: pin mask for each input, which
// means what pin is mapped to that OpenPLC input
int inBufferPinMask[MAX_INPUT] = {8, 9, 7, 0, 2, 3, 12, 13, 14, 21, 22, 23, 24, 25};

// outBufferPinMask: pin mask for each output, which
// means what pin is mapped to that OpenPLC output
int outBufferPinMask[MAX_OUTPUT] = {15, 16, 4, 5, 6, 10, 11, 26, 27, 28, 29};

// analogOutBufferPinMask: pin mask for the analog PWM
// output of the RaspberryPi
int analogOutBufferPinMask[MAX_ANALOG_OUT] = {1};
pthread_mutex_t bufferLock; // mutex for the internal buffers
uint8_t bool_input_buf[MAX_PI_IO];
uint8_t bool_output_buf[MAX_PI_IO];
uint16_t int_input_buf[MAX_PI_IO];
uint16_t int_output_buf[MAX_PI_IO];
//-----------------------------------------------------------------------------
// Verify if pin is present in one of the ignored vectors
//-----------------------------------------------------------------------------
int pinNotPresent(int *ignored_vector, int vector_size, int pinNumber)
{
    for (int i = 0; i < vector_size; i++)
    {
        if (ignored_vector[i] == pinNumber)
            return 0;
    }
    
    return 1;
}
//-----------------------------------------------------------------------------
// This function is called by the OpenPLC in a loop. Here the internal buffers
// must be updated to reflect the actual state of the output pins. The mutex buffer_lock
// must be used to protect access to the buffers on a threaded environment.
//-----------------------------------------------------------------------------
void updateBuffersWrite()
{
	printf("远端向树莓派输入数据");
	pthread_mutex_lock(&bufferLock); // lock mutex

	// OUTPUT
	for (int i = 0; i < MAX_OUTPUT; i++)
	{
		if (pinNotPresent(ignored_bool_outputs, ARRAY_SIZE(ignored_bool_outputs), outBufferPinMask[i]))
			digitalWrite(outBufferPinMask[i], bool_output_buf[i]);
	}

	// ANALOG OUT (PWM)
	for (int i = 0; i < MAX_ANALOG_OUT; i++)
	{
		if (pinNotPresent(ignored_int_outputs, ARRAY_SIZE(ignored_int_outputs), i))
			pwmWrite(analogOutBufferPinMask[i], (int_output[i] / 64));
	}

	pthread_mutex_unlock(&bufferLock); // unlock mutex
}
void handleWriteAction(cJSON *root)
{
	printf("远端向树莓派输入数据");


	// 获取二维数组
	cJSON *matrix = cJSON_GetObjectItemCaseSensitive(root, "bool_output");
	
	cJSON *intOutput = cJSON_GetObjectItem(root, "int_output");
	// 假设我们知道 matrix 的大小或者我们可以动态确定它的大小
	const int rows = cJSON_GetArraySize(matrix); // 行数
	pthread_mutex_lock(&bufferLock); // lock mutex
    if (matrix && matrix->type == cJSON_Array) {
        int size = cJSON_GetArraySize(matrix);
        
        for (int i = 0; i < size; i++) {
            cJSON* item = cJSON_GetArrayItem(matrix, i);

            if (item->type == cJSON_Number) {
                int value = cJSON_GetNumberValue(item);
				printf("index:%d value:%d\n ",i, value);
				bool_output_buf[i]=value;
            }
        }
    }
	int_output[0] = intOutput->valueint;
	pthread_mutex_unlock(&bufferLock); // unlock mutex
	updateBuffersWrite();
}
//-----------------------------------------------------------------------------
// This function is called by the main OpenPLC routine when it is initializing.
// Hardware initialization procedures should be here.
//-----------------------------------------------------------------------------
void initializeHardware()
{
	printf("初始化树莓派");
	wiringPiSetup();
	// piHiPri(99);

	// set pins as input
	for (int i = 0; i < MAX_INPUT; i++)
	{
		if (pinNotPresent(ignored_bool_inputs, ARRAY_SIZE(ignored_bool_inputs), inBufferPinMask[i]))
		{
			pinMode(inBufferPinMask[i], INPUT);
			if (i != 0 && i != 1) // pull down can't be enabled on the first two pins
			{
				pullUpDnControl(inBufferPinMask[i], PUD_DOWN); // pull down enabled
			}
		}
	}

	// set pins as output
	for (int i = 0; i < MAX_OUTPUT; i++)
	{
		if (pinNotPresent(ignored_bool_outputs, ARRAY_SIZE(ignored_bool_outputs), outBufferPinMask[i]))
			pinMode(outBufferPinMask[i], OUTPUT);
	}

	// set PWM pins as output
	for (int i = 0; i < MAX_ANALOG_OUT; i++)
	{
		if (pinNotPresent(ignored_int_outputs, ARRAY_SIZE(ignored_int_outputs), analogOutBufferPinMask[i]))
			pinMode(analogOutBufferPinMask[i], PWM_OUTPUT);
	}
}
void updateBuffersRead()
{
	pthread_mutex_lock(&bufferLock); // lock mutex
	// INPUT
	for (int i = 0; i < MAX_INPUT; i++)
	{
		if (pinNotPresent(ignored_bool_inputs, ARRAY_SIZE(ignored_bool_inputs), inBufferPinMask[i]))
			bool_input_buf[i] = digitalRead(inBufferPinMask[i]);
	}
	pthread_mutex_unlock(&bufferLock); // unlock mutex
}

char *handleReadAction()
{
	printf("树莓派向远端传输数据\n");
	char *json_str;
	// 创建JSON对象
	cJSON *root = cJSON_CreateObject();
	cJSON* matrix = cJSON_CreateArray();
	// Read raspberrypi GPIO
	updateBuffersRead();
    for (int i = 0; i < MAX_INPUT; i++) {
        cJSON* item = cJSON_CreateNumber(bool_input_buf[i]);
        cJSON_AddItemToArray(matrix, item);
    }
	cJSON_AddItemToObject(root,"bool_input",matrix);
	json_str = cJSON_Print(root);
	printf("%s\n", json_str);
	return json_str;
}
void *tcp_server_callBackFun(void *ptr)
{
	// int new_sockfd = (int *)ptr;//错误，不能直接使用地址，防止外部地址数值改变
	int client_sockfd = *(int *)ptr;
	printf("开启线程服务处理客户端(client_sockfd=%d)\n", client_sockfd);

	char recv_buff[1024 * 10] = {0};
	int recv_len = 0;

	char *str = NULL;

	while (1)
	{
		memset(recv_buff, 0, sizeof(recv_buff));

		recv_len = tcp_blocking_rcv(client_sockfd, recv_buff, sizeof(recv_buff)); // 堵塞接收消息
		if (0 > recv_len)
		{
			printf("接收客户端消息失败(client_sockfd=%d)!\n", client_sockfd);
			break;
		}
		else if (0 == recv_len)
		{
			printf("客户端断开连接(client_sockfd=%d)\n", client_sockfd);
			break;
		}
		else
		{
			printf("收到客户端消息(client_sockfd=%d):%s\n", client_sockfd, recv_buff);
			char *json_str;
			// 解析JSON字符串

			cJSON *root = cJSON_Parse(recv_buff);
			if (root == NULL)
			{
				// 处理解析错误
				printf("parse error");
				// close(newsockfd);
				// return -1;
				continue;
			}

			printf("获取run_model字段\n");
			cJSON *model = cJSON_GetObjectItem(root, "run_model");
			if (!cJSON_IsString(model) || (model->valuestring == NULL))
			{
				// 处理缺失或错误的"model"字段
				cJSON_Delete(root);
				continue;
			}

			// 比较"model"字段并调用相应的函数
			int result;
			if (strcmp(model->valuestring, "R") == 0)
			{
				printf("R-");
				json_str = handleReadAction();
			}
			else if (strcmp(model->valuestring, "W") == 0)
			{
				printf("W-");
				handleWriteAction(root);
				json_str = "{\"result\":\"OK\"}";
			}
			else
			{
				// 处理未知的"model"值
				result = -1;
			}
			tcp_send(client_sockfd, json_str, strlen(json_str));
		}
		// usleep(1*1000);
	}

	tcp_close(client_sockfd);

	printf("退出线程服务(client_sockfd=%d)\n", client_sockfd);
	return NULL;
}

int main(int argc, char *argv[])
{
	int ret = 0;
	initializeHardware();
	int sockfd = -1;
	sockfd = tcp_creat_socket(); // 创建socket
	if (0 > sockfd)
	{
		printf("socket创建失败...!\n");
		return -1;
	}

	int port = 8081;
	char *ip = (char *)"0.0.0.0";

	ret = tcp_server_bind_and_listen(sockfd, ip, port, 1024);
	if (0 > ret)
	{
		printf("bind_and_listen失败...!\n");
		tcp_close(sockfd);
		return -1;
	}
	printf("服务端ip=0.0.0.0, 端口=%d\n", port);

	int new_sockfd = -1;
	while (1)
	{
		if (0 > (new_sockfd = tcp_server_wait_connect(sockfd)))
		{ // 堵塞直到客户端连接
			printf("等待连接失败...!\n");
			continue;
		}
		else
		{
			printf("\n有客户端连接成功! new_sockfd=%d\n", new_sockfd);
			tcp_server_creat_pthread_process_client(&new_sockfd, tcp_server_callBackFun); // 服务端每接收到新的客户端连接,就创建新线程提供服务
		}
	}

	tcp_close(sockfd);

	return 0;
}
