#include <sys/socket.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/sendfile.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>

#define URI_SIZE 256
#define HTTP_OK "200 OK"

#define three_letter_cmp(method, ch1, ch2, ch3)	\
	method[0] == ch1 && method[1] == ch2 && method[2] == ch3

#define  four_letter_cmp(method, ch1, ch2, ch3, ch4) \
	method[0] == ch1 && method[1] == ch2 && method[2] == ch3 && method[3] == ch4

static const char errorMesg[] = 
"HTTP/1.1 404 Not Found\r\n"
"Content-Type: text/html; charset=UTF-8\r\n"
"Content-Length: 110\r\n"
"Connection: keep-alive\r\n"
"Keep-Alive: timeout=10, max=100\r\n"
"\r\n<HTML>\r\n"
"<HEAD><title>404 Not Found</title></HEAD>\r\n"
"<body><center><h1>404 Not Found</h1></center></body>\r\n"
"</HTML>";

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


void add_content_length(char *response, const ssize_t *file_size)
{
	char content_length[64];
	const uint8_t num_of_digits = snprintf(0, 0, "%ld", *file_size);
	snprintf(content_length, num_of_digits + 19,"Content-Length: %ld\r\n", *file_size);
	strncat(response, content_length, strlen(content_length));

	return;
}


void add_connection(char *response, const http_header_t *headers)
{
	uint8_t i = 0;
	char connection[57] = "";
	
	while(strncmp(headers[i].http_header, "Connection", 10))
		i++;

	if (!strncmp(headers[i].http_value, "keep-alive", 10))
		strncat(connection, "Connection: keep-alive\r\n", 24);
		//strncat(connection, "Connection: keep-alive\r\nKeep-Alive: timeout=100, max=100\r\n\0", 58);
	else
		strncat(connection, "Connection: close\r\n\0", 20);
	strncat(response, connection, strlen(connection));

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
{	/*this function compares extension of a file with extensions
	from the mime types file and adds it's mime type with a content-type header
	it also gets mime-encoding using linux comand - file*/
	char header[64];
	char *content_type;
	char *extension; 
	char *line;
	size_t n = 0;

	/*FILE *fp;
  	char output[512];
  	char *content_encoding;

   	char file_command_path[512] = "/usr/bin/file --mime-encoding /root/Desktop/Programmes/socket/webServ_v3.0/english.mirea.ru/";
  	strncat(file_command_path, path, strlen(path)); 

  	// open the command for reading
  	if ((fp = popen(file_command_path, "r")) == NULL)
  		dieWithError("popen() failed");

  	// read the output 
  	if (fgets(output, 512, fp) < 0)
  		dieWithError("fgets() failed");
  	content_encoding = memrchr(output, ' ', strlen(output));
  	content_encoding++;

  	pclose(fp);*/

	extension = memrchr(path, '.', strlen(path));
	extension++;
	/*The reason why i am setting last symbol as ':' is because i want to be sure
	that the type that i will read from file will exactly match with length of extension 
	that i've got, for example if extension is .js and i look through mime types file and find 
	"json:application/javascript" record, the program will work incorrectly,
	 but if i leave ':' at the end it will only concur with "js:application/javascript"*/	
	extension[strlen(extension)] = ':';

	fseek(mime_types_file, SEEK_SET, 0);
	while (getline(&line, &n, mime_types_file) != -1)
	{
		if (!strncmp(line, extension, strlen(extension)))
		{
			content_type = memchr(line, ':', strlen(line));
			content_type++;
			content_type[strlen(content_type) - 1] = '\0';

			bzero(&header, sizeof(header));
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
								   const ssize_t *size, 
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
	add_content_length(response, size);
	add_connection(response, headers);
	strncat(response, "\r\n", 2);

	return;
}


void parse_query_and_send_response(char *query, const uint16_t clntSock, 
												FILE *mime_types_file)
{
	char response[256] = {""};
	char *query_strings[11] = {""};
	char *first_space;
	char *second_space;
	char *header_delim;
	char string_delim[2] = "\n";
	struct stat file_info;
	http_header_t headers[11];
	http_start_string start_string;
	uint8_t i = 0;
	int16_t fd = 0;
	int64_t total = 0;

	//extract start string from the query
	query_strings[0] = strtok(query, string_delim);
	
	//headers = calloc(sizeof(http_header_t), i);

	//determine http method size, use later to identify which one exactly
	if ((first_space = memchr(query_strings[0], ' ',strlen(query_strings[0]))) < 0)
		dieWithError("[-]no spaces in first string");
	start_string.method_size = first_space - query_strings[0];
	first_space++;	
	if ((second_space = memrchr(query_strings[0], ' ', strlen(query_strings[0]))) < 0)
		dieWithError("[-]can't find second space");

	//determine path to a document and check if it exist
	++first_space;
	bzero(&start_string.path, sizeof(start_string.path));
	strncpy(start_string.path, first_space, second_space - first_space);

#ifdef VERBOSE
	printf("\n[<-] request: %s\n", start_string.path);	
	fflush(stdout);
#endif

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
			printf("[-]No such file\n");
			if (send(clntSock, errorMesg, strlen(errorMesg), 0) < 0)
					dieWithError("send() failed");
			return;			
		}
		if (!S_ISREG(file_info.st_mode))
		{
			printf("[-]Requested file isn't a regular file\n");
			if (send(clntSock, errorMesg, strlen(errorMesg), 0) < 0)
					dieWithError("send() failed");
			return;
		}
		if ((fd = open(start_string.path, O_RDONLY)) < 0)
			dieWithError("open() failed");
	}

	switch (start_string.method_size)
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
			dieWithError("[-]unknown method");
	} 
	start_string.http_protocol = ++second_space;
	start_string.http_protocol[strlen(start_string.http_protocol) - 1] = '\0';
	bzero(&headers, sizeof(headers));

	//separate strings in a query, than divide them on headers and values
	while(1)
	{
		i++;
		query_strings[i] = strtok(NULL, string_delim);

		//separate until "\n" string
		if (strlen(query_strings[i]) == 1) 
			break;

		header_delim = memchr(query_strings[i], ':', strlen(query_strings[i]));
		strncpy(headers[i-1].http_header, query_strings[i], header_delim - query_strings[i]);
		strncpy(headers[i-1].http_value , header_delim+2, strlen(header_delim));
	}
	form_response(response, &start_string, headers, &file_info.st_size, mime_types_file);

#ifdef VERBOSE
	printf("[->]response:%m\n%s", response);
	fflush(stdout);
#endif
	if (setsockopt(clntSock, IPPROTO_TCP, TCP_QUICKACK, &(uint32_t){1}, sizeof(uint32_t)) < 0)
	 	dieWithError("setsockopt() failed");
	if (setsockopt(clntSock, IPPROTO_TCP, TCP_CORK, &(uint32_t){1}, sizeof(uint32_t)) < 0)
		 dieWithError("setsockopt() failed");
	if (send(clntSock, response, strlen(response), 0) < 0)
		dieWithError("send() failed");
	while(total < file_info.st_size)
	{
		if (sendfile(clntSock, fd, &total, file_info.st_size - total) < 0)
		{
			if ((errno == EWOULDBLOCK) || (errno == EAGAIN))
				break;
			else
				dieWithError("sendfile() failed");
		}
	}
	if (setsockopt(clntSock, IPPROTO_TCP, TCP_CORK, &(uint32_t){0}, sizeof(uint32_t)) < 0)
		dieWithError("setsockopt() failed");
#ifdef VERBOSE			
	printf("[->]<%x>Send on sock:%d size:%ld file:%s\n\n", getpid(), clntSock, total, start_string.path);
	fflush(stdout);
#endif	
	close(fd);

	return;
}