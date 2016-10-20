#ifndef __PROXY_H__

#include "cache.h"

/*Helper functions */

struct request_info {
    char method[32];
	char version[32];
	char port[16];
	char host[LINE];
	char path[MAXLINE];
	char hdrs[MAXLINE];
	char entity[MAXLINE];
	int cont_length;
	//length in bytes represents the length of the requesst entity body;
	int no_cache;
};

struct response_info {
	int scode;
	int cacheable;
	int resp_len;
	struct tm expire;
	char response[MAX_OBJECT_SIZE + 16];
	char reloc[MAXLINE];
};

struct file_dps {
	int client_fd;
	int server_fd;	
};

struct thread_info {
	struct request_info *req_ptr;
	struct response_info *resp_ptr;
	struct file_dps *fdp; 
};

void *proxy_thread(void *vargp);
inline void process_request(struct thread_info *);
void req_line_parse(struct thread_info *, char *); 
void header_parse(struct thread_info *);
int connect_server(struct thread_info *);
void commute_server(struct thread_info *);
void reconnect_server(struct thread_info *);
void response_parse(struct thread_info *);
void find_cache(struct thread_info *);
inline void thread_clean(struct thread_info *);
int readline_client(struct thread_info *, char *, int);
int read_client(struct thread_info *, char *, int);
int read_server(struct thread_info *, char *, int);
int write_2_client(struct thread_info *, char *, int);
int write_2_server(struct thread_info *, char *, int);
//static int rio_read(rio_t *, char *, int);
inline char *upper_string(char *p);
inline int sgetline(char *dest, char *src);
inline int str2time(const char *s, struct tm *tm);
inline void error_msg(const char *message);
void sigpipe_handler(int sig);
void sigint_handler(int sig);
void print_request(struct request_info *);
void print_response(struct response_info *);

#endif
