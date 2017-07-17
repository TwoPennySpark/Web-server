#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/epoll.h>
#include <unistd.h>
#include "config.h"
#include "parse.c"

#define BUFFER_SIZE 2048
#define MAX_CLIENT 128

int main(const int const argc, const char* const argv[])
{
	uint16_t listenSock;
	int16_t clntSock;
	int16_t ready;
	int16_t efd;
	int32_t recvSize;
	socklen_t clntLen;
	FILE *mime_types_file;
	char buffer[BUFFER_SIZE];
	struct sockaddr_in clntAddr;
	struct epoll_event event;
	struct epoll_event events[MAX_CLIENT];

	listenSock = atoi(argv[0]);
	printf("<%x>\n", getpid());
	if ((mime_types_file = fopen(MIME_TYPE_FILE, "r")) < 0)
		dieWithError("fopen() failed");

	bzero(&clntAddr, sizeof(clntAddr));
	if ((efd = epoll_create(MAX_CLIENT)) < 0)
		dieWithError("epoll_create() failed");

	/* add a monitor on the listenSock to the epoll instance 
	 * associated with efd, per the events defined in event
	 * (edge-triggered behaviour, available to read without blocking)
	 */
	event.data.fd = listenSock;
	event.events = EPOLLIN | EPOLLET;
	if (epoll_ctl(efd, EPOLL_CTL_ADD, listenSock, &event) < 0)
		dieWithError("epoll_ctl() on listenSock failed");

	for (;;)
	{
#ifdef VERBOSE
		printf("<%x>before epoll_wait\n", getpid());
		fflush(stdout);
#endif 
		if ((ready = epoll_wait(efd, events, MAX_CLIENT, -1)) < 0)
			dieWithError("epoll_wait() failed");
#ifdef VERBOSE
		printf("<%x>epoll_wait:[%d]\n",getpid(), ready);
		fflush(stdout);
#endif 
		for (uint16_t i = 0; i < ready;i++)
		{
			if ((!(events[i].events & EPOLLIN)) || (events[i].events & EPOLLERR) || (events[i].events & EPOLLHUP))
			{//error occured
#ifdef VERBOSE
				printf("[1]<%x>epoll error:%m\n", getpid());
				fflush(stdout);
#endif			
				shutdown(events[i].data.fd, 2);
				close(events[i].data.fd);
				continue;
			}
			else if (listenSock == events[i].data.fd)
			{
				/* we have a notification on listenSock which means we have
				 * one or more incoming connections
				 * we use cycle because epoll_wait will only work once on listen 
				 * socket even if there is multiply conections incoming
				 */
				while(1)
				{
#ifdef VERBOSE
					printf("[2]Ready: %d listenSock:%d\n", ready, listenSock);
					fflush(stdout);
#endif
					clntLen = sizeof(clntAddr);
					if ((clntSock = accept4(listenSock, (struct sockaddr*)&clntAddr, &clntLen, SOCK_NONBLOCK)) < 0)
					{//break when all connections are handled
						if ((errno == EAGAIN) || (errno == EWOULDBLOCK))
						{
#ifdef VERBOSE
							printf("[*]<%x>EAGAIN or EWOULDBLOCK\n"
							"--------------------------------------------------------------------------------------\n", getpid());
							fflush(stdout);
#endif
							break;
						}
						else
							dieWithError("accept() failed");
					}
#ifdef VERBOSE
					printf("[!]New connection from IP:%s port:%d\n", inet_ntop(AF_INET, 
																	&clntAddr.sin_addr.s_addr, buffer, BUFFER_SIZE),
																	ntohs(clntAddr.sin_port));
					fflush(stdout);
#endif

					/* disable Nagle's algotithm, forcing a socket to send 
					 * the data in it's buffer, whatever the packet size is
					 */ 
					if (setsockopt(clntSock, IPPROTO_TCP, TCP_NODELAY, &(uint32_t){1}, sizeof(uint32_t)) < 0)
						dieWithError("setsockopt() failed");

				   	/* add a monitor on the clntSock to the epoll instance 
					 * associated with efd, per the events defined in event
					 * (edge-triggered behaviour, available for reading without blocking)
					 */
					event.data.fd = clntSock;
					event.events = EPOLLIN | EPOLLET;
					if (epoll_ctl(efd, EPOLL_CTL_ADD, clntSock, &event) < 0)
						dieWithError("epoll_ctl() on clntSock failed");
#ifdef VERBOSE
					printf("[*]<%x>Added {%d}\n", getpid(), clntSock);
					fflush(stdout);
#endif
				}
				if ((ready-i-1) <= 0)
				 	break;
			}
			else
			{//if there is data on the client socket
				while(1)
				{
#ifdef VERBOSE
					printf("[3]<%x>Data on clntSock\nevents[%d].data.fd = %d\n", getpid(), i, events[i].data.fd);
					fflush(stdout);
#endif
					if (setsockopt(events[i].data.fd, IPPROTO_TCP, TCP_QUICKACK, &(uint32_t){1}, sizeof(uint32_t)) < 0)
						dieWithError("TCP_QUICKACK failed");
					bzero(&buffer, BUFFER_SIZE);
					if ((recvSize = recv(events[i].data.fd, buffer, BUFFER_SIZE, 0)) < 0)
					{
						// If errno == EWOULDBLOCK, that means we have read all data
						if ((errno == EAGAIN) || (errno == EWOULDBLOCK))
						{
#ifdef VERBOSE
							printf("[*]Read all data\n"
							"--------------------------------------------------------------------------------------\n");
							fflush(stdout);
#endif							
							break;
						}
						else 
							dieWithError("recv() failed");
					}
					else if (recvSize == 0)
					{
#ifdef VERBOSE						
						//the remote socket has closed the connection
						printf("[*]<%x>:Remote socket has closed the connection\n", getpid());
						fflush(stdout);
#endif
						shutdown(events[i].data.fd, 2);
						close(events[i].data.fd);
						break;
					}
					if (parse_query_and_send_response(buffer, events[i].data.fd, mime_types_file) < 0)
						break;
				}
			}
		}
	}
	fclose(mime_types_file);
	return 0;
}
