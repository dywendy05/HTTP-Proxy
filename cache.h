#ifndef __CACHE_H__

#define MAX_CACHE_SIZE 1024000 // Originally 1049000;
#define MAX_OBJECT_SIZE 102400
#define LINE 1024
#define MAX_BUF_SIZE (MAX_CACHE_SIZE + 16)

struct node {
	char data[MAX_OBJECT_SIZE];
	char host[LINE];
	char path[LINE];
	struct tm *expire;
	int len;
    struct node * next;
};

struct cache {
	struct node * head; // List of available nodes;
	int size;
};

void cach_init();
struct node * cach_add(char * host, char * path, 
              struct tm *expire, char *body, int len);
struct node * cach_insert(char *host, char *path, 
                struct tm *expire, char *body, int len);
void cach_delete(struct node *p, struct node *prev);
struct node * cach_search( char *host, char *path, 
                          struct node **prev);
void cach_free();

#endif
