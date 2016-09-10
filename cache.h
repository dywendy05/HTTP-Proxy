#ifndef __CACHE_H__

#define MAX_CACHE_SIZE 1024000 // Originally 1049000;
#define MAX_OBJECT_SIZE 102400
#define LINE 1024
#define MAX_BUF_SIZE (MAX_CACHE_SIZE + 16)

struct node {
	char data[MAX_OBJECT_SIZE]; // Where cache is stored;
	char host[LINE];
	char path[LINE];
	struct tm *expire;
	int len;
    struct node * next;
	struct node * prev; //Not used for nodes in the invalid list;
};

// Struct for both the valid list and the invalid list;
struct cache {
	struct node * head; 
	struct node * tail;	// Set to NULL for the invalid list;
	int size; // Size of all valid caches; Set to 0 for the invalid list;
};

void cach_init();
struct node * cach_add(char * host, char * path, 
					   struct tm *expire, char *body, int len);
void cach_delete(struct node *p);
struct node * cach_search( char *host, char *path);
void cach_free();

#endif
