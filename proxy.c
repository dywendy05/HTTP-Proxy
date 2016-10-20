#define _XOPEN_SOURCE

#include <stdio.h>
#include <fcntl.h>
#include <time.h>
#include <math.h>
#include "csapp.h"
#include "proxy.h"

#define DEBUG
#ifdef DEBUG
# define dbg_printf(...) printf(__VA_ARGS__)
# define dbg_print_request(...) print_request(__VA_ARGS__)
# define dbg_print_response(...) print_response(__VA_ARGS__)
#else 
# define dbg_printf(...)
# define dbg_print_request(...)
# define dbg_print_response(...)
#endif

#define min(a, b) (((a) < (b)) ? (a) : (b))

static const char *user_agent = "User-Agent: Mozilla/5.0 (X11; Linux x86_64; "
                                "rv:10.0.3) Gecko/20120305 Firefox/10.0.3\r\n";
static const char *accept_file = "Accept: text/html,application/xhtml+xml,"
                                 "application/xml;q=0.9,*/*;q=0.8\r\n";
static const char *accept_encoding = "Accept-Encoding: gzip, deflate\r\n"; 

sem_t mutex;

int main(int argc, char **argv)
{
    printf("%s%s%s", user_agent, accept_file, accept_encoding);

    if(argc < 2)
        printf("Input: %s proxy listening port number\n", argv[0]);
   
    int port = atoi(argv[1]);
	int listen_fd;
	socklen_t clientlen;
	int connect_fd;
	struct sockaddr_in clientaddr;
	pthread_t tid;

	listen_fd = Open_listenfd(port);
	Signal(SIGPIPE, sigpipe_handler);
	Signal(SIGINT, sigint_handler);
    Sem_init(&mutex, 0, 1); /*mutex = 1*/
	cach_init();
    
	while(1) {
		connect_fd = accept(listen_fd, (struct sockaddr *)&clientaddr, 
							&clientlen);
		Pthread_create(&tid, NULL, proxy_thread, (void *)(size_t)connect_fd);
	}
	return 0;
}

void *proxy_thread(void *vargp)
{
	dbg_printf("\n\nThis is a new thread\n");
    struct file_dps fds = {(int)(size_t)vargp, -1};
	Pthread_detach(pthread_self());
	
	struct request_info *req = (struct request_info *) 
							   malloc(sizeof(struct request_info));
	if(req == NULL){
		close(fds.client_fd);
		error_msg("WARNING: Malloc failure");
	}
	memset(req, 0, sizeof(struct request_info));

	struct thread_info info = {req, NULL, &fds};

	process_request(&info);
	dbg_print_request(req);

    //Cache part;
	if(!(req->no_cache))
		find_cache(&info);

    //Cache NOT hit, connect the server:	
	struct response_info *resp = (struct response_info *) 
								 malloc(sizeof(struct response_info));
	if(resp == NULL) {
		thread_clean(&info);
		error_msg("WARNING: Malloc failure");
	}
	memset(resp, 0, sizeof(struct response_info));
	resp->cacheable = 1;
	info.resp_ptr = resp;

	commute_server(&info);
	
    //If cacheable than cache the response;
	if(resp->cacheable) { 
		P(&mutex);
		cach_add(req->host,req->path, &(resp->expire),resp->response,
				 resp->resp_len);
		V(&mutex);
	}
	thread_clean(&info);
    return NULL;
}

inline void process_request(struct thread_info *info)
{
	char line[MAXLINE];
	readline_client(info, line, MAXLINE); 
	dbg_printf("Request line is: %s", line);

	req_line_parse(info, line);
	header_parse(info);
	return;
}

void req_line_parse(struct thread_info *info, char *line)
{
	char uri[MAXLINE];
	struct request_info *req = info->req_ptr;

    sscanf(line, "%s %s %s", req->method, uri, req->version);	
	upper_string(req->method);

	char *p, *dest;
	if((p = strstr(uri, "://")))
		p += strlen("://");
	else 
		p = uri;
	//Host:
	dest = req->host;
	while((*p != '/') && (*p != ':') && (*p != '\0'))
		*dest++ = *p++;
	*dest = '\0';
	//Path:
	dest = req->path;
	while((*p != '/') && (*p != '\0'))
		++p;
	if(*p == '\0')
		*dest++ = '/';
	else {
		while((*p != ':') && (*p != '\0'))
			*dest++ = *p++;
	}
	*dest = '\0';
	//Port:
	dest = req->port;
	if((p = strstr(uri, "://"))) {
		p += strlen("://");
		p = strstr(p, ":");
	}
	else {
		p = strstr(uri, ":");
	}
	if(p) {
		++p;
		while(isdigit(*p))
			*dest++ = *p++;
		*dest = '\0';
	}
	if(*(req->port) == '\0')	
        strcpy(req->port, "80");

	//Do not search/store cache if the method is neither GET nor HEAD;
	if(strcmp(req->method, "GET") && strcmp(req->method, "HEAD"))
		req->no_cache = 1;

	return;
}

// Return 1 if conditions for no-cache-search and not-cacheable are met, 0 o/w;
void header_parse(struct thread_info *info)
{
    char line[2048];
	char name[32];
	char value[2048];
	int hflag = 0; // Whether HOST is in the headers;
	struct request_info *req = info->req_ptr;
	while(readline_client(info, line, 1024) > 0 ) {
		if((!strcmp(line, "\r\n")) || (!strcmp(line, "\n")))
			break;
	    sscanf(line, "%s %s", name, value);
		upper_string(name);
	
		if(!strcmp(name, "HOST:")) 
			hflag = 1;
		else if(!strcmp(name, "CONTENT-LENGTH:")) 
			req->cont_length = atoi(value);
		if(!strcmp(name, "AUTHORIZATION:") ||
		   !strcmp(name, "IF-MODIFIED-SINCE:") ||
		   (!strcmp(name, "PRAGMA:") && 
		    strstr(upper_string(value), "NO-CACHE") != NULL)
		   ) 
			req->no_cache = 1;
		else if(!strcmp(name, "USER-AGENT:")) 
			continue;
		else if(!strcmp(name, "ACCEPT:")) 
			continue;
		else if(!strcmp(name, "ACCEPT-ENCODING:")) 
			continue;
		else if(!strcmp(name, "CONNECTION:")) 
			continue;
		else if(!strcmp(name, "PROXY-CONNECTION:")) 
			continue;
		strcat(req->hdrs, line);
	}
	//Add host info if no host in the request header;
    if(!hflag) {
		if(!strcmp(req->port, "80"))
		    sprintf(req->hdrs, "Host: %s\r\n%s", req->host, req->hdrs);
	    else
			sprintf(req->hdrs, "Host: %s:%s\r\n%s", 
					req->host, req->port, req->hdrs);
	}
	strcat(req->hdrs, user_agent);
	strcat(req->hdrs, accept_file);
	strcat(req->hdrs, accept_encoding);
	sprintf(req->hdrs, "%sConnection: close\r\nProxy-Connection: close\r\n\r\n"
					   , req->hdrs);	

	return; 
}

// Return -1 on error, and the file descriptor of the server otherwise;
int connect_server(struct thread_info *info)
{
    int socket_fd;
	struct addrinfo hints, *serverinfo, *p;
	
	memset(&hints, 0, sizeof(hints));
	hints.ai_flags = AI_CANONNAME;
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;

	struct request_info *req = info->req_ptr;
	if((getaddrinfo(req->host, req->port, &hints, &serverinfo)) != 0)
        return -1; 

	for(p = serverinfo; p != NULL; p = p->ai_next) {
	    if((socket_fd = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) 
			< 0)
			continue;
		if((connect(socket_fd, p->ai_addr, p->ai_addrlen)) < 0)
			continue;  
		break;  
	}
	freeaddrinfo(serverinfo);

	dbg_printf("p is 0x%p, socket_fd is %d\n", p, socket_fd);
	if(p == NULL)
		return -1;

	return socket_fd;
} 
	
void commute_server(struct thread_info *info)
{
	struct request_info *req = info->req_ptr;
	struct response_info *resp = info->resp_ptr;
	struct file_dps *fds = info->fdp;
	//Connect server and send request line and headers to it;
	if((fds->server_fd = connect_server(info)) == -1) {
		thread_clean(info);
		error_msg("Server connection failure");
	}

	char line[MAXLINE];
	sprintf(line, "%s %s %s\r\n", req->method, req->path, req->version);
	dbg_printf("Request line sent to server is %s\n", line);
	write_2_server(info, line, strlen(line));
	write_2_server(info, req->hdrs, strlen(req->hdrs));

	//Send the request entity (if any) to the server;
	int n = req->cont_length;
	while(n > 0) {
			int cnt_read = read_client(info, req->entity, MAXLINE);
			write_2_server(info, req->entity, cnt_read);
			n -= cnt_read;
	}

	// Read response from server;
	resp->resp_len = read_server(info, resp->response, MAX_OBJECT_SIZE + 16);
	if(resp->resp_len > MAX_OBJECT_SIZE) 
		resp->cacheable = 0;

	response_parse(info);
	dbg_print_response(resp);
	
	// Status code in (300, 301, 302) indicates redirection, 
	// in which case we'll retry connection to the server;
	// We only cache MAXLINE in bytes of the request entity line, beyond that 
	// we'll have to reopen another thread;
	if(((resp->scode == 300) || (resp->scode == 301) || (resp->scode == 302)) 
		&& (req->cont_length <= MAXLINE)) {
		reconnect_server(info);
		dbg_print_response(resp);
	}
	write_2_client(info, resp->response, resp->resp_len);
	while((n = read_server(info, line, MAXLINE)) > 0)
		write_2_client(info, line, n);

	return;
}

void reconnect_server(struct thread_info *info)
{
	dbg_printf("Reconnection revokded\n");
	char line[MAXLINE];
	struct request_info *req = info->req_ptr;
	struct response_info *resp = info->resp_ptr;
	struct file_dps *fds = info->fdp;
	sprintf(line, "%s %s %s\r\n", req->method, resp->reloc, req->version);
	req_line_parse(info, line);
	if(!(req->no_cache)) 
		find_cache(info);

	close(fds->server_fd);
	if((fds->server_fd = connect_server(info)) == -1) {
		thread_clean(info);
		error_msg("Server connection failure");
	}		

	memset(line, 0, MAXLINE);
	sprintf(line, "%s %s %s\r\n", req->method, req->path, 
			req->version);
	dbg_printf("New request: %s %s %s\n", req->method, req->path,req->version);

	write_2_server(info, line, strlen(line));
	write_2_server(info, req->hdrs, strlen(req->hdrs));
	if(req->cont_length > 0) 
		 write_2_server(info, req->entity, req->cont_length);

	resp->resp_len = read_server(info, resp->response, MAX_OBJECT_SIZE + 16);
	if(resp->resp_len > MAX_OBJECT_SIZE)
		resp->cacheable = 0;

	response_parse(info);
	return;
}

/*Parse the response in the sting response, 
   and fill in the values of expire and cach_flag; */
void response_parse(struct thread_info *info)
{
	struct response_info *resp = info->resp_ptr;
	char line[MAXLINE];
	char vers[16];
	char code[16];
	char phr[16];
	int n;
	char *str = resp->response;
	char *p;
	struct tm *expire = &(resp->expire);
	struct tm date;
	time_t now;
	struct tm *cur_time;
	// Get the current time;
	time(&now);
	cur_time = gmtime(&now);
	memset(&date, 0, sizeof(struct tm));
	
	if((n = sgetline(line, str)) > 0) {
		dbg_printf("Response line is: %s\n", line);
		sscanf(line, "%s %s %s", vers, code, phr);
		resp->scode = atoi(code);
		str += n;
	}
	while((n = sgetline(line, str)) > 0) {
	    if(!strcmp(line, "\r\n"))
			break;			
		if((strstr(line, "Location"))) {
			char *dest = resp->reloc;
			p = line + strlen("Location:");
			//remove spaces and \r\n;
			while((*p != '\0') && isspace(*p))
				++p;
			while((*p != '\0') && !isspace(*p) && (*p != '\r')&& (*p != '\n'))
				*dest++ = *p++;

			*dest = '\0';
			break;
		}
	    if(strstr(line, "WWW-Authenticate:")) 
            resp->cacheable = 0;
		else if((p = strstr(line, "Expires:"))) {
		    p += strlen("Expires:");
			if(str2time(p, expire) || 
				((expire->tm_year < cur_time->tm_year)|| 
					((expire->tm_year == cur_time->tm_year) &&
					(expire->tm_yday <=  cur_time->tm_yday)))  
				)// Expire date is not valid or is before or at today;
				resp->cacheable = 0;
		}
		else if((p = strstr(line, "Date:"))) {
			p += strlen("Date:");
			str2time(p, &date);
        }
		str += n;
	} 
	if(resp->scode == 304)
		resp->cacheable = 0;
    if((date.tm_year > 0) && (expire->tm_year > 0) &&
		((expire->tm_year < date.tm_year) || 
		((expire->tm_year == date.tm_year) &&
		(expire->tm_yday <= date.tm_yday)))
		) // If Expire is before Date, do not cache;
	    resp->cacheable = 0;   

	return;
}

void find_cache(struct thread_info *info)
{
	struct request_info *req = info->req_ptr;
    struct node *nd; 	
	time_t now;
	struct tm *cur_time;
	time(&now);
	cur_time = gmtime(&now);
	P(&mutex);
	if((nd = cach_search(req->host, req->path)) != NULL) {
			//Delete the cache copy if it has expired;
		    if((nd->expr_year > 0) && 
				((nd->expr_year < cur_time->tm_year) || 
				((nd->expr_year == cur_time->tm_year) && 
				(nd->expr_day <= cur_time->tm_yday)))) 
                cach_delete(nd); 
			else { //hit;
				dbg_printf("HIT\n");
				write_2_client(info, nd->data, nd->len);
				thread_clean(info);
				pthread_exit(NULL);
			}
	}
	V(&mutex);
	return;
}

inline void thread_clean(struct thread_info *info)
{
	free(info->req_ptr);
	free(info->resp_ptr);

	struct file_dps *fds = info->fdp;
	close(fds->client_fd);
	if(fds->server_fd >= 0)
		close(fds->server_fd);	
	return;
}

int readline_client(struct thread_info *info, char *buf, int len)
{
	int nleft = len;
	int rc;
	char c, *p = buf;
	int fd = (info->fdp)->client_fd;
	while(nleft > 0) {
		if((rc = read(fd, &c, 1)) == 1) {
			*p++ = c;
			--nleft;
			if(c == '\n') 
				break;
		} else if(rc == 0) {
			break;
		} else {
			if(errno != EINTR) {
				thread_clean(info);
				error_msg("Fail to read from client(-1)");
			}
		}
	}
	*p = 0;	
	if(len == nleft) {
		thread_clean(info);
		error_msg("Fail to read from client(0)");
	}
	return (len - nleft);
}

int read_client(struct thread_info *info, char *buf, int len)
{
	int nleft = len;
	int nread;
	char *p = buf;
	int fd = (info->fdp)->client_fd;
	while(nleft > 0) {
		if((nread = read(fd, p, nleft)) < 0) {
			if(errno == EINTR)
				nread = 0;
			else {
				thread_clean(info);
				error_msg("Fail to read from client(-1)");
			}	
		}
		else if(nread == 0)
			break;
		nleft -= nread;
		p += nread;
	}
	if(len == nleft) {
		thread_clean(info);
		error_msg("Fail to read from client(0)");
	}
	return (len - nleft);
}

int read_server(struct thread_info *info, char *buf, int len)
{
	int nleft = len;
	int nread;
	char *p = buf;
	int fd = (info->fdp)->server_fd;
	while(nleft > 0) {
		if((nread = read(fd, p, nleft)) < 0) {
			if(errno == EINTR)
				nread = 0;
			else {
				thread_clean(info);
				error_msg("Fail to read from server(-1)");
			}	
		}
		else if(nread == 0)
			break;
		nleft -= nread;
		p += nread;
	}	
	if(len == nleft) {
		thread_clean(info);
		error_msg("Fail to read from server(0)");
	}
	return (len - nleft);
}

int write_2_client(struct thread_info *info, char *buf, int len)
{
	int nleft = len;
	int nwritten;
	int fd = (info->fdp)->client_fd;
	char *p = buf;
	while(nleft > 0) {
		if((nwritten = write(fd, p, nleft)) <= 0) {
			if(errno == EINTR)
				nwritten = 0;
			else {
				thread_clean(info);
				error_msg("Fail to write to client");
			}
		}
		nleft -= nwritten;
		p += nwritten;
	}
	return len;
}

int write_2_server(struct thread_info *info, char *buf, int len)
{
    int nleft = len;
	int nwritten;
	int fd = (info->fdp)->server_fd;
	char *p = buf;
	while(nleft > 0) {
		if((nwritten = write(fd, p, nleft)) <= 0) {
			if(errno == EINTR)
				nwritten = 0;
			else {
				thread_clean(info);
				error_msg("Fail to write to server");
			}
		}
		nleft -= nwritten;
		p += nwritten;
	}
	return len;
}

inline char *upper_string(char *ptr)
{
	char *p = ptr;
    while((*p) != '\0') {
	    *p = toupper(*p);
		p++;
	}
	return ptr;
}

//Getline from string src and put the line in dest, including the newline char;
inline int sgetline(char *dest, char *src)
{
	int c;
	int n = 0;
    while(((c = *src) != '\n') && (c != '\0')) { 
	    *dest++ = *src++;
		++n;
	}
    if(c == '\n') {
		*dest++ = '\n';
		++n;
	}	
	*dest = '\0';
	return n;
}

// Return 0 on success and -1 on error;
// Fill in the struct tm pointed by tm using info from string s;
inline int str2time(const char *s, struct tm *tm)
{
	if((strptime(s, "%n%a, %d %b %Y %H:%M:%S", tm) != NULL) 
		&& (strptime(s, "%n%A, %d-%b-%y %H:%M:%S", tm) != NULL) 
		&& (strptime(s, "%n%a %b %n%d %H:%M:%S %Y", tm) != NULL)
		)
		return -1;
	
    return 0;
}

// Send the error message back to fd, then close it and exit the thread;
inline void error_msg(const char *message)
{
	printf("%s\n", message);
    pthread_exit(NULL);
}

void sigpipe_handler(int sig)
{
    printf("Broken PIPE!\n");
	return;
}

void sigint_handler(int sig)
{
    printf("\nSIGINT received!\n");
	cach_free();
	exit(0);
	return;
}

void print_request(struct request_info *req)
{
	printf("Metainfo of the request: \n");
	printf("method is %s, version is %s, port is %s and host+path is %s %s\n", 
		   req->method, req->version, req->port, req->host, req->path);
	printf("cont_len is %d, no_cache is %d\n", req->cont_length,req->no_cache);
	printf("header is:\n%s\n", req->hdrs);
	return;
}
void print_response(struct response_info *resp)
{
	printf("Metainfo of the response: \n");
	printf("status code is %d, resp_len is %d and cacheable is %d\n",
		   resp->scode, resp->resp_len, resp->cacheable);
	printf("relocation is %s\n", resp->reloc);
	printf("The expire year is %d and the expire day is %d\n", 
	       (resp->expire).tm_year, (resp->expire).tm_yday);
	return;
}
