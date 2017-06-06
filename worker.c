#define _GNU_SOURCE
#include <stdlib.h>
#include <stdio.h>
#include <sys/epoll.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>
#include <stdint.h>
#include "config.h"
#include "parse.c"

#define BUFFER_SIZE 2048
#define MAX_CLIENT 64


int main(const int const argc, const char* const argv[argc+1])
{
	uint16_t listenSock;
	int16_t clntSock;
	int16_t flags;
	int16_t ready;
	int16_t efd;
	int32_t recvSize;
	socklen_t clntLen;
	FILE *mime_types_file;
	char buffer[BUFFER_SIZE];
	struct sockaddr_in clntAddr;
	struct epoll_event event;
	struct epoll_event* events;

	listenSock = atoi(argv[0]);
	printf("<%x>\n", getpid());
	if ((mime_types_file = fopen(MIME_TYPE_FILE, "r")) < 0)
		dieWithError("fopen() failed");

	bzero(&clntAddr, sizeof(clntAddr));
	if ((efd = epoll_create(MAX_CLIENT)) < 0)
		dieWithError("epoll_create() failed");

	/*add a monitor on the socket listenSock to the epoll instance 
	associated with efd, per the events defined in event
	(edge-triggered behaviour, available to read without blocking)*/
	event.data.fd = listenSock;
	event.events = EPOLLIN | EPOLLET;
	if (epoll_ctl(efd, EPOLL_CTL_ADD, listenSock, &event) < 0)
		dieWithError("epoll_ctl() on listenSock failed");

	events = calloc(MAX_CLIENT, sizeof(event));
	for (;;)
	{
#ifdef VERBOSE
		printf("<%x>before epoll_wait:%m\n", getpid());
		fflush(stdout);
#endif 
		if ((ready = epoll_wait(efd, events, MAX_CLIENT, -1)) < 0)
			dieWithError("epoll_wait() failed");
#ifdef VERBOSE
		printf("<%x>epoll_wait:%m\n", getpid());
		fflush(stdout);
#endif 
		for (int i = 0; i < ready;i++)
		{
			if ((!(events[i].events & EPOLLIN)) || (events[i].events & EPOLLERR) || (events[i].events & EPOLLHUP))
			{//Error occured
#ifdef VERBOSE
				printf("[1]<%x>epoll error:%m\n", getpid());
				fflush(stdout);
#endif			
				//shutdown(events[i].data.fd, 2);
				//close(events[i].data.fd);
				continue;
			}
			else if (listenSock == events[i].data.fd)
			{//We have a notification on listenSock which means we have one or more incoming connections
				while(1)
				{
#ifdef VERBOSE
					printf("[2]Ready: %d listenSock:%d\n", ready, listenSock);
					fflush(stdout);
#endif
					clntLen = sizeof(clntAddr);
					if ((clntSock = accept(listenSock, (struct sockaddr*)&clntAddr, &clntLen)) < 0)
					{//break when all connections are handled
						if ((errno == EAGAIN) || (errno == EWOULDBLOCK))
						{
#ifdef VERBOSE
							printf("[*]EAGAIN or EWOULDBLOCK\n");
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
					
					//set new socket nonblocking
					if ((flags = fcntl(clntSock, F_GETFL)) < 0)
						dieWithError("fcntl() failed");
					flags |= O_NONBLOCK;
					if (fcntl(clntSock, F_SETFL, flags) < 0)
						dieWithError("fcntl() failed");

					/*add a monitor on the socket clntSock to the epoll instance 
					associated with efd, per the events defined in event
					(edge-triggered behaviour, available for reading without blocking)*/
					event.data.fd = clntSock;
					event.events = EPOLLIN | EPOLLET;
					if (epoll_ctl(efd, EPOLL_CTL_ADD, clntSock, &event) < 0)
						dieWithError("epoll_ctl() on clntSock failed");
#ifdef VERBOSE
					printf("[*]<%x>Added {%d}\n", getpid(), clntSock);
					fflush(stdout);
#endif
					if (--ready <= 0)
						break;
				}
			}
			else
			{
				while(1)
				{
#ifdef VERBOSE
					printf("[3]<%x>Data on clntSock\nevents[%d].data.fd = %d\n", getpid(), i, events[i].data.fd);
					fflush(stdout);
#endif
					bzero(&buffer, BUFFER_SIZE);
					if ((recvSize = recv(events[i].data.fd, buffer, BUFFER_SIZE, 0)) < 0)
					{
						fflush(stdout);
						// If errno == EAGAIN, that means we have read all data
						if ((errno == EAGAIN) || (errno == EWOULDBLOCK))
						{
#ifdef VERBOSE
							printf("[*]{%m}Read all data\n");
							fflush(stdout);
#endif							
							break;
						}
						else if (errno == ECONNRESET)
						{
#ifdef VERBOSE
							printf("[*]Connection on socket %d reset by client\n", events[i].data.fd);
							fflush(stdout);
#endif
							shutdown(events[i].data.fd, 2);
							close(events[i].data.fd);
							break;
						}
						else 
							dieWithError("recv() failed");
					}
					else if (recvSize == 0)
					{
#ifdef VERBOSE						
						// End of file. The remote socket has closed the connection
						printf("[*]<%x>: End of file\n", getpid());
						fflush(stdout);

#endif
						shutdown(events[i].data.fd, 2);
						close(events[i].data.fd);
						break;
					}
					parse_query_and_send_response(buffer, events[i].data.fd, mime_types_file);

					if (--ready <= 0)
						break;
					/*shutdown(events[i].data.fd, 2);
					close(events[i].data.fd);
					break;*/
				}
			}
		}
	}
	free(events);
	fclose(mime_types_file);
	return 0;
}

/*EAGAIN is often raised when performing non-blocking I/O. It means "there is no data available right now, try again later".
  It might (or might not) be the same as EWOULDBLOCK, which means "your thread would have to block in order to do that".
  EAGAIN & EWOULDBLOCK: Operation would have caused the process to be suspended.*/
/*strdup() to copy whole string into the other string and strcpy() to copy part of the string*/
/*Can't set timeout for non-blocking sockets*/
/*There are 2 types of http connections: persistent and non-persistent(those are opening new tcp connection each time it request for a file)*/
/*If you writing a server with a select() function waiting for connection before accept() call, than it might happen that after select 
will be triggered, the program will not immediately call accept() and if in that amount of time client sends RST flag, than the program 
will block in accept() until next connection*/
