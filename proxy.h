#ifndef __PROXY_H__

#include "cache.h"

/*Helper functions */

struct request_info {
    char method[32];
	char version[32];
	char host[LINE];
	char path[LINE];
	int port;
};

void *proxy_thread(void *vargp);
int req_line_parse(char * line, struct request_info * p); 

int connect_server(struct request_info * req);
void dec2str(int src, char * dest);
int header_parse(rio_t * rp, struct request_info *req, char *body);
void response_parse(char *, struct tm **, int *, int *, char *);
int sgetline(char *dest, char *src);
int str2time(const char *s, struct tm *tm);
char * upper_string(char *p);
void error_msg(int fd, const char *message);
void sigpipe_handler(int sig);
void sigint_handler(int sig);


#endif
