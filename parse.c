#include <sys/socket.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/sendfile.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>

#define URI_SIZE 256
#define NUMBER_OF_HEADERS 11
#define RESPONSE_HTTP_HEADER_SIZE 256
#define HTTP_OK "200 OK"

#define three_letter_cmp(method, ch1, ch2, ch3)	\
	method[0] == ch1 && method[1] == ch2 && method[2] == ch3

#define  four_letter_cmp(method, ch1, ch2, ch3, ch4) \
	method[0] == ch1 && method[1] == ch2 && method[2] == ch3 && method[3] == ch4

static const char not_found_msg[] = 
"HTTP/1.1 404 Not Found\r\n"
"Content-Type: text/html; charset=UTF-8\r\n"
"Content-Length: 110\r\n"
"Connection: keep-alive\r\n"
"Keep-Alive: timeout=10, max=100\r\n"
"\r\n<html>\r\n"
"<head><title>404 Not Found</title></head>\r\n"
"<body><center><h1>404 Not Found</h1></center></body>\r\n"
"</html>";

static const char bad_request_msg[] = 
"HTTP/1.1 400 Bad Request\r\n"
"Content-Type: text/html; charset=UTF-8\r\n"
"Content-Length: 118\r\n"
"Connection: close\r\n"
"\r\n<html>\r\n"
"<head><title>400 Bad Request</title></head>\r\n"
"<body><center><h1>400 Bad Request</h1></center><body>\r\n"
"</html>";

typedef enum
{
	HTTP_GET = 0,
	HTTP_PUT,
	HTTP_POST,
	HTTP_HEAD,
}method;

typedef struct 
{
	uint8_t http_method;
	uint8_t method_size;
	char path[URI_SIZE];
	char *http_protocol;
}http_start_string;

typedef struct 
{
	char http_header[20];
	char http_value[512];
}http_header_t;


void dieWithError(const char *msg)
{
	perror(msg);
	exit(-1);
}

void send_bad_request(uint16_t clntSock, char *reason)
{
#ifdef VERBOSE
	printf("[-]%s\n", reason);
	fflush(stdout);
#endif
	if (send(clntSock, bad_request_msg, strlen(bad_request_msg), 0) < 0)
		dieWithError("send() failed");
	shutdown(clntSock, 2);
	close(clntSock);
	return ;
}

void add_content_length(char *response, const ssize_t *file_size)
{
	char content_length[64] = "";
	const uint8_t num_of_digits = snprintf(0, 0, "%ld", *file_size);
	snprintf(content_length, num_of_digits + 19,"Content-Length: %ld\r\n", *file_size);
	strncat(response, content_length, strlen(content_length));

	return;
}

void add_connection(char *response, const http_header_t *headers)
{
	for (uint8_t i = 0; i < NUMBER_OF_HEADERS; i++)
	{
		if (!strncmp(headers[i].http_header, "Connection", 10))
		{
			if (!strncmp(headers[i].http_value, "keep-alive", 10))
			{
				strncat(response, "Connection: keep-alive\r\n", 24);
				//strncat(response, "Connection: keep-alive\r\nKeep-Alive: timeout=100, max=100\r\n\0", 58);
				return;
			}
			else
			{
				strncat(response, "Connection: close\r\n", 19);
				return;
			}
		}
	}
	return;
}

void add_accept_ranges(char *response)
{
	char accept_ranges[] = "Accept-Ranges: bytes\r\n";
	strncat(response, accept_ranges, strlen(accept_ranges));

	return;
}

void add_content_type(char *response, const char *path, 
									  FILE *mime_types_file)
{	/* this function compares extension of a file with extensions
	 * from the mime types file and adds it's mime type with a content-type header
	 */
	char header[64] = "";
	char *content_type;
	char *extension; 
	char *line;
	size_t n = 0;

	extension = memrchr(path, '.', strlen(path));
	extension++;
	/* The reason why i am setting last symbol as ':' is because i want to be sure
	 * that the type that i will read from file will exactly match with length of extension 
	 * that i've got, for example if extension is .js and i look through mime types file and find 
	 * "json:application/javascript" record, the program will work incorrectly,
	 * but if i leave ':' at the end it will only concur with "js:application/javascript"
	 */	
	extension[strlen(extension)] = ':';

	fseek(mime_types_file, SEEK_SET, 0);
	while (getline(&line, &n, mime_types_file) != -1)
	{
		if (!strncmp(line, extension, strlen(extension)))
		{
			content_type = memchr(line, ':', strlen(line));
			content_type++;
			content_type[strlen(content_type) - 1] = '\0';

			snprintf(header, strlen(content_type) + 17, "Content-Type: %s\r\n", content_type);
			//snprintf(header, strlen(content_type) + strlen(content_encoding) + 25,
			//						 "Content-Type: %s; charset=%s", content_type, content_encoding);
			strncat(response, header, strlen(header));
			break;
		}
	}

	return;
}

void form_response(char *response, const http_start_string *start_string, 
								   const http_header_t *headers, 
								   const ssize_t *fileSize, 
								   FILE *mime_types_file)
{
	switch (start_string->http_method)
	{
		case 0:
		{
			snprintf(response, strlen(HTTP_OK) + strlen(start_string->http_protocol) + 4,
									"%s %s\r\n", start_string->http_protocol, HTTP_OK);
			break;	
		}
	}
	add_accept_ranges(response);
	add_content_type(response, start_string->path, mime_types_file);
	add_content_length(response, fileSize);
	add_connection(response, headers);
	strncat(response, "\r\n", 2);

	return;
}

int parse_query_and_send_response(char *query, const uint16_t clntSock, 
												FILE *mime_types_file)
{
	char *first_space;
	char *second_space;
	char *header_delim;
	char *query_strings[NUMBER_OF_HEADERS] = {""};
	char response[RESPONSE_HTTP_HEADER_SIZE] = "";
	http_header_t headers[NUMBER_OF_HEADERS];
	http_start_string start_string;
	struct stat file_info;
	uint8_t i = 1;
	int16_t fd = 0;
	int16_t flags = 0;
	int64_t offset = 0;
	char delim = '\n';

	//extract start string from the query
	if ((query_strings[0] = strtok(query, &delim)) == NULL)
	{
		send_bad_request(clntSock, "strtok() failed");
		return -1;
	}
	
#ifdef VERBOSE
	printf("\n[<-] request:%s\n", query);	
	fflush(stdout);
#endif

	//determine http method size, use later to identify which one exactly
	if ((first_space = memchr(query_strings[0], ' ',strlen(query_strings[0]))) < 0)
	{//[-]no space in the 1st string
		send_bad_request(clntSock, "[-]{memchr() failed} No space in the 1st string");
		return -1;
	}
	start_string.method_size = first_space - query_strings[0];
	if ((second_space = memrchr(query_strings[0], ' ', strlen(query_strings[0]))) == first_space)
	{//[-]can't find second space
		send_bad_request(clntSock, "[-]{memrchr() failed} Can't find second space");
		return -1;
	}

	//determine path to a document and check if it exist
	first_space += 2;
	bzero(&start_string.path, sizeof(start_string.path));
	strncpy(start_string.path, first_space, second_space - first_space);

	if (!strlen(start_string.path))
	{
		if (stat(MAIN_PAGE, &file_info) < 0)
		 	dieWithError("[-]Can't access the main page");
		if ((fd = open(MAIN_PAGE, O_RDONLY)) < 0)
			dieWithError("open() failed");
		strncat(start_string.path, MAIN_PAGE, strlen(MAIN_PAGE));
	}
	else
	{
		if (stat(start_string.path, &file_info) < 0)
		{
#ifdef VERBOSE
			printf("[-]No such file:{%s}\n", start_string.path);
			fflush(stdout);
#endif
			if (send(clntSock, not_found_msg, strlen(not_found_msg), 0) < 0)
					dieWithError("send() failed");
			return 0;			
		}
		if (!S_ISREG(file_info.st_mode))
		{
			printf("[-]Requested file isn't a regular file\n");
			if (send(clntSock, not_found_msg, strlen(not_found_msg), 0) < 0)
					dieWithError("send() failed");
			return 0;
		}
		if ((fd = open(start_string.path, O_RDONLY)) < 0)
			dieWithError("open() failed");
	}

	//determine http method
	switch(start_string.method_size)
	{
		case 3:
			{
				if (three_letter_cmp(query_strings[0], 'G', 'E', 'T'))
				{	
					start_string.http_method = HTTP_GET;
					break;
				}
				
				if (three_letter_cmp(query_strings[0], 'P', 'U', 'T'))
				{
					start_string.http_method = HTTP_PUT;
					break;
				}
			}
		case 4:
			{
				if (four_letter_cmp(query_strings[0], 'P', 'O', 'S', 'T'))
				{
					start_string.http_method = HTTP_POST;
					break;
				}

				if (four_letter_cmp(query_strings[0], 'H', 'E', 'A', 'D'))
				{
					start_string.http_method = HTTP_HEAD;
					break;
				}
			}
		default:
		{
			send_bad_request(clntSock, "[-]unknown method");
			return -1;
		}
	}

	start_string.http_protocol = ++second_space;
	start_string.http_protocol[strlen(start_string.http_protocol) - 1] = '\0';
	bzero(&headers, sizeof(headers));

	//separate strings in a query, than divide them on headers and values
	while((query_strings[i] = strtok(NULL, &delim)))
	{
		//separate until "\n" string
		if ((header_delim = memchr(query_strings[i], ':', strlen(query_strings[i]))) == NULL)
			break;

		strncpy(headers[i-1].http_header, query_strings[i], header_delim - query_strings[i]);
		strncpy(headers[i-1].http_value , header_delim+2, strlen(header_delim));
		i++;
	}
	form_response(response, &start_string, headers, &file_info.st_size, mime_types_file);

#ifdef VERBOSE
	printf("[->]response:%m\n%s", response);
	fflush(stdout);
#endif

	/* disabling non-blocking mode when transfering files with size > 100kB
	 * because one average call to sendfile function with non-blocking socket 
	 * transfers < 100kB without EWOULDBLOCK error, the transfer of larger files  
	 * will cause program to call sendfile function from 10 to 500 times, every
	 * time returning with EWOULDBLOCK error
	 */
	if (file_info.st_size > 100000)
		if (fcntl(clntSock, F_SETFL, flags) < 0)
			dieWithError("fcntl() failed");

	if (setsockopt(clntSock, IPPROTO_TCP, TCP_QUICKACK, &(uint32_t){1}, sizeof(uint32_t)) < 0)
	 	dieWithError("setsockopt() failed");

	//using TCP_CORK to combine http header and sent data
	if (setsockopt(clntSock, IPPROTO_TCP, TCP_CORK, &(uint32_t){1}, sizeof(uint32_t)) < 0)
		 dieWithError("setsockopt() failed");

	//send http header	
	if (send(clntSock, response, strlen(response), 0) < 0)
	{
		if (errno == ECONNRESET)
		{
#ifdef VERBOSE
			printf("[*]Connection on socket %d reset by client\n", clntSock);
			fflush(stdout);
#endif
			shutdown(clntSock, 2);
			close(clntSock);
			return -1;
		}
		else
			dieWithError("send() failed");
	}
	
	//send file
	while(offset < file_info.st_size)
	{
		if (sendfile(clntSock, fd, &offset, file_info.st_size - offset) < 0)
		{
			if ((errno == EWOULDBLOCK) || (errno == EAGAIN))
			{
#ifdef VERBOSE				
				printf("EWOULDBLOCK or EAGAIN\n");	
				fflush(stdout);
#endif
				break;
			}
			else
				dieWithError("sendfile() failed");
		}
#ifdef VERBOSE
		printf("offset = %ld fileSize - offset = %ld\n", offset, file_info.st_size - offset);
		fflush(stdout);
#endif 	
 	}

	if (setsockopt(clntSock, IPPROTO_TCP, TCP_CORK, &(uint32_t){0}, sizeof(uint32_t)) < 0)
		dieWithError("setsockopt() failed");

 	if (file_info.st_size > 100000)
 	{
	 	if ((flags = fcntl(clntSock, F_GETFL)) < 0)
			dieWithError("fcntl() failed");
		flags |= O_NONBLOCK;
		if (fcntl(clntSock, F_SETFL, flags) < 0)
			dieWithError("fcntl() failed");
	}
#ifdef VERBOSE			
	printf("[->]<%x>Send on sock:%d file:%s\n\n", getpid(), clntSock, start_string.path);
	fflush(stdout);
#endif	
	close(fd);

	return 0;
}