/*
* Author: Marek Maślanka
* Project: DEKU
* URL: https://github.com/MarekMaslanka/deku
*/

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <netdb.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define BUFSIZE 1024
#define MAXERRS 16

extern char **environ;

static void error(char *msg)
{
	perror(msg);
	exit(1);
}

static void cerror(FILE *stream, char *cause, char *errno,
			char *shortmsg, char *longmsg)
{
	fprintf(stream, "HTTP/1.1 %s %s\n", errno, shortmsg);
	fprintf(stream, "Content-type: text/html\n");
	fprintf(stream, "\n");
	fprintf(stream, "<html><title>Error</title>");
	fprintf(stream, "<body bgcolor="
					"ffffff"
					">\n");
	fprintf(stream, "%s: %s\n", errno, shortmsg);
	fprintf(stream, "<p>%s: %s\n", longmsg, cause);
	fprintf(stream, "<hr><em>Web server</em>\n");
}

int main(int argc, char **argv)
{

	int parentfd;
	int childfd;
	int portno;
	int clientlen;
	struct hostent *hostp;
	char *hostaddrp;
	int optval;
	struct sockaddr_in serveraddr;
	struct sockaddr_in clientaddr;

	FILE *stream;
	char buf[BUFSIZE];
	char method[BUFSIZE];
	char uri[BUFSIZE];
	char version[BUFSIZE];
	char filename[BUFSIZE];
	char cgiargs[BUFSIZE];
	char *p;
	int fd;
	int pid;
	int wait_status;

	portno = 8090;

	parentfd = socket(AF_INET, SOCK_STREAM, 0);
	if (parentfd < 0)
		error("ERROR opening socket");

	optval = 1;
	setsockopt(parentfd, SOL_SOCKET, SO_REUSEADDR,
			   (const void *)&optval, sizeof(int));

	bzero((char *)&serveraddr, sizeof(serveraddr));
	serveraddr.sin_family = AF_INET;
	serveraddr.sin_addr.s_addr = htonl(INADDR_ANY);
	serveraddr.sin_port = htons((unsigned short)portno);
	if (bind(parentfd, (struct sockaddr *)&serveraddr,
			 sizeof(serveraddr)) < 0)
		error("ERROR on binding");

	if (listen(parentfd, 1) < 0)
		error("ERROR on listen");

	clientlen = sizeof(clientaddr);
	while (1)
	{
		childfd = accept(parentfd, (struct sockaddr *)&clientaddr, (socklen_t *)&clientlen);
		if (childfd < 0)
			error("ERROR on accept");

		hostp = gethostbyaddr((const char *)&clientaddr.sin_addr.s_addr,
							  sizeof(clientaddr.sin_addr.s_addr), AF_INET);
		if (hostp == NULL)
			error("ERROR on gethostbyaddr");
		hostaddrp = inet_ntoa(clientaddr.sin_addr);
		if (hostaddrp == NULL)
			error("ERROR on inet_ntoa\n");

		if ((stream = fdopen(childfd, "r+")) == NULL)
			error("ERROR on fdopen");

		fgets(buf, BUFSIZE, stream);
		sscanf(buf, "%s %s %s\n", method, uri, version);

		if (strcasecmp(method, "GET"))
		{
			cerror(stream, method, "501", "Not Implemented",
				   "Tiny does not implement this method");
			fclose(stream);
			close(childfd);
			continue;
		}

		sprintf(buf, "HTTP/1.1 200 OK\n\n");
		write(childfd, buf, strlen(buf));

		pid = fork();
		if (pid < 0)
		{
			perror("ERROR in fork");
			exit(1);
		}
		else if (pid > 0)
		{
			wait(&wait_status);
		}
		else
		{
			close(0);
			dup2(childfd, 1);
			dup2(childfd, 2);
            char *argv[] = {"make", "deploy"};
			if (execve("/usr/bin/make", argv, environ) < 0)
			{
				perror("ERROR in execve");
			}
		}

		fclose(stream);
		close(childfd);
	}
}
