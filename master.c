#include <sys/types.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <errno.h>
#include <strings.h>
#include <netinet/tcp.h>
#include "config.h"

#define MAX_CLIENT 64

void dieWithError(const char *msg)
{
	perror(msg);
	exit(-1);
}

int main(const int const argc, const char ** const argv)
{
	int16_t listenSock;
	int16_t flags;
	int32_t status;
	struct sockaddr_in servAddr;
	pid_t pid[NUMBER_OF_PROCESSES];
	char listenSock_str[4];

	if ((listenSock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)) < 0)
		dieWithError("socket() failed");

	bzero(&servAddr, sizeof(servAddr));
	servAddr.sin_family = AF_INET;
	servAddr.sin_addr.s_addr = INADDR_ANY;
	servAddr.sin_port = htons(LPORT);

	//allow reuse of port even if it's in TIME_WAIT state
	if (setsockopt(listenSock, SOL_SOCKET, SO_REUSEADDR, &(uint32_t){1}, sizeof(uint32_t)) < 0)
		dieWithError("setsockopt() failed");

	if (bind(listenSock, (struct sockaddr*)&servAddr, sizeof(servAddr)) < 0)
		dieWithError("bind() failed");

	if ((flags = fcntl(listenSock, F_GETFL)) < 0)
		dieWithError("fcntl() failed");
	flags |= O_NONBLOCK;
	if (fcntl(listenSock, F_SETFL, flags) < 0)
		dieWithError("fcntl() failed");
	
	if (listen(listenSock, MAX_CLIENT) < 0)
		dieWithError("listen() failed");

	/* convert listenSock to string so we can pass it to 
	 * the child processes using execvp with 'args' array
	 */
	snprintf(listenSock_str, sizeof(listenSock), "%d", listenSock);
	char *args[] = {listenSock_str, NULL};

	//create worker processes
	for (uint8_t i = 0;i < NUMBER_OF_PROCESSES; i++)
	{
		if ((pid[i] = fork()) < 0)
			dieWithError("fork() failed");
		if (pid[i] == 0)
		{
			if (execvp(PATH_TO_WORKER, args) < 0)
				dieWithError("execvp() failed");
			exit(0);
		}
	}

	//wait for processes to finish
	for (uint8_t i = 0;i < NUMBER_OF_PROCESSES; i++)
	{
		waitpid(pid[i], &status, 0);
		printf("Child with PID: %x finished with status:%d\n", pid[i], status);
	}

	shutdown(listenSock, 2);
	close(listenSock);

	return 0;
}