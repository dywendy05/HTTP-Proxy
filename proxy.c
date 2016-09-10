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
#else 


# define dbg_printf(...)
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
    int client_fd = (int)(size_t)vargp;
	Pthread_detach(pthread_self());
    
	//Flags for cache;
	int search_cache = 1;
	int cacheable = 1;
	int hit = 0; 

    //Local buffers;
   	char line[MAXLINE];
	char body[MAXLINE];
	char hdrs[MAXLINE];
	char location[MAXLINE];
	char local_buf[MAX_BUF_SIZE];
	int buf_len;
    
	// Request line and header parse;
	rio_t rio_client;
	rio_readinitb(&rio_client, client_fd);
	if(rio_readlineb(&rio_client, line, MAXLINE) < 0) {
	    close(client_fd);
		pthread_exit(NULL);
	}
  	 
	dbg_printf("\n\n\nRequest received: %s\n", line);
	  
    struct request_info req;
	memset(&req, 0, sizeof(struct request_info));

    req_line_parse(line, &req);

	sprintf(body, "%s %s HTTP/1.0\r\n", req.method, req.path);

	if(header_parse(&rio_client, &req, hdrs) || 
	   (strcmp(req.method, "GET") && strcmp(req.method, "HEAD"))) {
	    search_cache = 0;
		cacheable = 0;
	} 
	// Do not search/store cache if the request method is neither GET nor HEAD;
	strcat(body, hdrs);

    //Cache part;
    struct node * nd; 
	time_t now;
	struct tm *cur_time;
	time(&now);
	cur_time = gmtime(&now);
	
	if(search_cache) {
        P(&mutex);

		//Hit;
	    if((nd = cach_search(req.host, req.path)) 
			!= NULL) {
			//Delete the cache copy if it has expired;
		    struct tm *expire = nd->expire;
		    if((expire != NULL) && 
				((expire->tm_year < cur_time->tm_year) || 
				((expire->tm_year == cur_time->tm_year) && 
				(expire->tm_yday <= cur_time->tm_yday))) ) 
                cach_delete(nd); 
			else {
			    hit = 1; 
				buf_len = nd->len;
				memcpy(local_buf, nd->data, buf_len); //Copy to a local buffer;
			}
		}
		V(&mutex);
	}
    
    dbg_printf("search_cache is %d, hit is %d\n", search_cache, hit);

	if(hit) {
	    rio_writen(client_fd, local_buf, buf_len);
		close(client_fd);
		pthread_exit(NULL);	
	}

    //Cache NOT hit, connect the server:
	int server_fd;
	if((server_fd = connect_server(&req)) == -1) 
		error_msg(client_fd, "Server connection failure");

	char entity[MAXLINE];
	if(rio_writen(server_fd, body, strlen(body)) <= 0) {
		close(server_fd);
		error_msg(client_fd, "Server broke!");

		int n = req.cont_length;
		if(n > MAXLINE) {
			int cnt_read = 0;
			// Send the entity body (if any) in request to server line by line;
			while(n > 0) {
				if((cnt_read = 
					rio_readlineb(&rio_client, line, min(n, MAXLINE)))<=0) {
					close(client_fd);
					pthread_exit(NULL);
				}
				if(rio_writen(server_fd, line, cnt_read) <= 0) {
					close(server_fd);
					error_msg(client_fd, "Server broke!");
				}
				n -= cnt_read;
			}
		}
		else if(n > 0) {
			if(rio_readlineb(&rio_client, entity, n) <= 0) {
				close(client_fd);
				pthread_exit(NULL);
			}
			if(rio_writen(server_fd, entity, n) <= 0) {
				close(server_fd);
				error_msg(client_fd, "Server broke!");	
			}
		}
	}

    rio_t rio_server;
    rio_readinitb(&rio_server, server_fd);

	int n;
	struct tm *expire;
	if((n = rio_readnb(&rio_server, local_buf, MAX_BUF_SIZE - 1)) <= 0) {
		close(server_fd);
		error_msg(client_fd, "Server broke!");
	}
	local_buf[n] = '\0';
	int rd_flg; // redirection flag;
	response_parse(local_buf, &expire, &cacheable, &rd_flg, location);

	// Status code(300, 301, 302) in response indicates redirection, 
	// in which case we'll retry connection to the server;
	if(rd_flg && (req.cont_length <= MAXLINE)) {
		sprintf(line, "%s %s HTTP/1.0\r\n", req.method, location);
		req_line_parse(line, &req);
		close(server_fd);

		if((server_fd = connect_server(&req)) == -1) 
			error_msg(client_fd, "Server connection failure");
	
		memset(body, 0, sizeof(body));
		sprintf(body, "%s %s HTTP/1.0%s", req.method, req.path, hdrs);
		dbg_printf("New request: %s %s HTTP/1.0\n", req.method, req.path);
	
		if((rio_writen(server_fd, body, strlen(body)) <= 0) ||
			(req.cont_length > 0 && 
			 rio_writen(server_fd, entity, req.cont_length) <= 0)) {
			close(server_fd);
			error_msg(client_fd, "Server broke!");
		}
		rio_readinitb(&rio_server, server_fd);
		if((n = rio_readnb(&rio_server, local_buf, MAX_BUF_SIZE - 1)) <= 0) {
			close(server_fd);
			error_msg(client_fd, "Server broke!");
		}
		local_buf[n] = '\0';
		response_parse(local_buf, &expire, &cacheable, NULL, location);
	}

	if(expire != NULL) {
        char str[100];
		strftime(str, 100, "%a, %d %b %Y %H:%M:%S", expire);
		dbg_printf("Expire time is %s\n", str);
	}

    dbg_printf("cacheable = %d, n is %d\n", cacheable, n);
    
	if(cacheable && (n <= MAX_OBJECT_SIZE)) { 
		P(&mutex);
	    if((nd =  cach_add(req.host, req.path, expire, local_buf, n))
			== NULL) {
		    printf("Fatal error: out of memory!\n");
		    cach_free();	
			exit(0);
		}
		V(&mutex);
	}
    
	do {
	    if(write(client_fd, local_buf, n) <= 0) {
		    close(client_fd);
			close(server_fd);
			pthread_exit(NULL);
		}
	} while((n = rio_readnb(&rio_server, local_buf, MAX_BUF_SIZE)) > 0);

	close(server_fd);
	close(client_fd);
    return NULL;
}

void req_line_parse(char * line, struct request_info * p) 
{
	char uri[MAXLINE];
	char port[24];
    sscanf(line, "%s %s %s", p->method, uri, p->version);	
	upper_string(p->method);

	char *sp1, *sp2, *sp3;
	if((sp1 = strstr(uri, "://")))
		sp1 += strlen("://");
	else
		sp1 = uri;

	if((sp2 = strstr(sp1, ":")) != NULL) {
	    if((sp3 = strstr(sp2, "/")) == NULL)
			strcpy(p->path, "/");
		else {
		    strcpy(p->path, sp3);
			*sp3 = '\0';	
		}
		sp2 ++;
		strcpy(port, sp2);
		p->port = atoi(port);
		*(sp2 - 1) = '\0';
    }
	else {
	    if((sp3 = strstr(sp1, "/")) == NULL)
			strcpy(p->path, "/");
	    else {
		    strcpy(p->path, sp3);
			*sp3 = '\0';
		}
        p->port = 80;
	}
	
	strcpy(p->host, sp1);

	return;
}

// Return -1 on error, and the file descriptor of the server otherwise;
int connect_server(struct request_info * req)
{
    int socket_fd;
	struct addrinfo hints, *serverinfo, *p;
	
	memset(&hints, 0, sizeof(hints));
	hints.ai_flags = AI_CANONNAME;
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;

	char service[16];

	if(req->port)
		dec2str(req->port, service); 
	else strcpy(service, "http");	

	if((getaddrinfo(req->host, service, &hints, &serverinfo)) != 0)
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

	if(p == NULL)
		return -1;

	return socket_fd;
} 
	

void dec2str(int src, char * dest)
{
    int cnt = 0;
    int num = src;
    while(num > 0) {
        num = num/10;
		cnt++;    
    }  
    
	num = src;
	int i;
	for(i = cnt - 1; i>= 0; i--) {
	    dest[i] = 48 + num - ((num/10) * 10);
		num = num/10;	
	} 

	dest[cnt] = '\0';
	
	return;
	
}

// Return 1 if conditions for no-cache-search and not-cacheable are met, 0 o/w;
int header_parse(rio_t * rp, struct request_info *req, char *headers)
{
    char line[MAXLINE];
	char name[32];
	char value[128];
	int hflag = 0;
	int no_cach_flag = 0;

	req->cont_length = 0;
//	int hd_log_fd = open(HEADER_LOG, O_WRONLY, 0);
	while((rio_readlineb(rp, line, MAXLINE)) > 0 ) {
//		rio_writen(hd_log_fd, line, MAXLINE);
		if((!strcmp(line, "\r\n")) || (!strcmp(line, "\n")))
			break;
	    sscanf(line, "%s %s", name, value);
		upper_string(name);
	
		if(!strcmp(name, "HOST:")) 
			hflag = 1;
		else if(!strcmp(name, "CONTENT-LENGTH:")) 
			req->cont_length = atoi(value);
		else if(!strcmp(name, "AUTHORIZATION:")) 
			no_cach_flag = 1;
		else if(!strcmp(name, "IF-MODIFIED-SINCE:"))
			no_cach_flag = 1;
		else if(!strcmp(name, "PRAGMA:")) {
			if(strstr(upper_string(value), "NO-CACHE") != NULL)
				no_cach_flag = 1;
		}
		else if(!strcmp(name, "USER-AGENT:"))
			break;
		else if(!strcmp(name, "ACCEPT:"))
			break;
		else if(!strcmp(name, "ACCEPT-ENCODING:"))
			break;
		else if(!strcmp(name, "CONNECTION:"))
			break;
		else if(!strcmp(name, "PROXY-CONNECTION:"))
			break;
		strcat(headers, line);
	}

	//Add host info if no host in the request header;
    if(!hflag) {
		char *p;
		if((p = strstr(req->host, "://")))
			strcpy(value, p + strlen("://"));
		else 
			strcpy(value, req->host);
		if(req->port == 80)
		    sprintf(headers, "%sHost: %s\r\n", headers, value);
	    else
			sprintf(headers, "%sHost: %s:%d\r\n", headers, value, req->port);
	}
	
    strcat(headers, user_agent);
	strcat(headers, accept_file);
	strcat(headers, accept_encoding);
	sprintf(headers, "%sConnection: close\r\nProxy-Connection: close\r\n\r\n",
			headers);	
	
	if(no_cach_flag)
		return 1;

	dbg_printf("%s \n", req->host);

	return 0;
}

char * upper_string(char *ptr)
{
	char *p = ptr;
    while((*p) != '\0') {
	    *p = toupper(*p);
		p++;
	}
	return ptr;
}

/* Parse the response in the sting response, 
   and fill in the values of expire and cach_flag; */
void response_parse(char *response, struct tm **expire_ptr, 
				    int *cach_flag, int *rd_flg, char *location)
{
	char line[MAXLINE];
	char vers[16];
	char code[16];
	char phr[16];
	int n;
	int expire_flg = 0;
	char *str = response;
	char *p;
	struct tm *expire;
	struct tm date;
	time_t now;
	struct tm *cur_time;
	time(&now);
	cur_time = gmtime(&now);
	
	*expire_ptr = NULL;
	memset(&date, 0, sizeof(struct tm));
	
	if(rd_flg && ((n = sgetline(line, str)) > 0)) {
		sscanf(line, "%s %s %s", vers, code, phr);
		//Multiple Choices; Moved Permanently; Moved Temporarily;
		if(atoi(code) == 300 || atoi(code) == 301 || atoi(code) == 302)
			*rd_flg = 1;
		else 
			*rd_flg = 0;

		str += n;
	}

	if(rd_flg && (*rd_flg == 1)) {
		while((n = sgetline(line, str)) > 0) {
			if(!strcmp(line, "\r\n"))
				break;
			if((strstr(line, "Location"))) {
				char * p = line + strlen("Location:");
				//remove spaces and \r\n;
				while(*p != '\0' && isspace(*p))
					++p;
				strcpy(location, p);
				while(*p != '\0' && !isspace(*p) && *p != '\r' && *p != '\n')
					++p;
				*p = '\0';
				break;
			}
			str += n;
			dbg_printf("%s\n", line);
		}
		return;
	}

	while((n = sgetline(line, str)) > 0) {
	    if(!strcmp(line, "\r\n"))
		break;
	    if(strstr(line, "WWW-Authenticate:")) 
            *cach_flag = 0;
		else if((p = strstr(line, "Expires:"))) {
		    p += strlen("Expires:");

			if((expire = malloc(sizeof(struct tm))) == NULL) {
			    printf("Out of memory!\n");
				cach_free();
				exit(0);
			}
			expire_flg = 1;
	        *expire_ptr = expire;
			memset(expire, 0, sizeof(struct tm));

			if(str2time(p, expire) < 0 ) //Not a valid expire time;
				*cach_flag = 0;
            
			else if((expire->tm_year < cur_time->tm_year)
					|| 
					((expire->tm_year == cur_time->tm_year) &&
					(expire->tm_yday <=  cur_time->tm_yday))
					)  // Expire date is before or at today;
				*cach_flag = 0;

		}
		else if((p = strstr(line, "Date:"))) {
			p += strlen("Date:");
			str2time(p, &date);
        }
		
		str += n;
	} 

    if(expire_flg && 
		((expire->tm_year < date.tm_year) // Expire before Date;
		||
		((expire->tm_year == date.tm_year) &&
		(expire->tm_yday <= date.tm_yday)))
		)
	    *cach_flag = 0;
    
	return;
}

//Getline from string src and put the line in dest, including the newline char;
int sgetline(char *dest, char *src)
{
	int c;
	int n = 0;
    while(((c = *src) != '\n') && (c != '\0')) { 
	    *dest++ = *src++;
		n++;
	}
    if(c == '\n') {
		*dest++ = '\n';
		n++;
	}
	
	*dest = '\0';
	return n;
}

/* Return 0 on success and -1 on error; 
   Fill in the struct tm pointed by tm using info from string s; */
int str2time(const char *s, struct tm *tm)
{
	if((strptime(s, "%n%a, %d %b %Y %H:%M:%S", tm) != NULL) 
		&& (strptime(s, "%n%A, %d-%b-%y %H:%M:%S", tm) != NULL) 
		&& (strptime(s, "%n%a %b %n%d %H:%M:%S %Y", tm) != NULL)
		)
		return -1;
	
    return 0;
}

// Send the error message back to fd, then close it and exit the thread;
void error_msg(int fd, const char *message)
{
	rio_writen(fd, (void *)message, strlen(message));
	rio_writen(fd, "\r\n", 2);
	close(fd);
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
