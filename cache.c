/*
1. The cache buffer is created at the beginning;
2. A node list contains nodes which have info such as host, path 
   and a pointer to the cache buffer where the real cache object is stored;
3. There are 2 node list, one containing available nodes, 
   the other containing the deleted nodes; 
   Once a cache copy expires, or is no longer valid, 
   we move its node from the available list to the deleted list. 
4. Every time we need to cache an object, we first check the deleted list, 
   if not empty we would cache the objet in the buffer pointed by 
   the first deleted node, and move that node to the available list.
*/
#include <stdio.h>
#include <string.h>
#define _XOPEN_SOURCE
#include <time.h>
#include <stdlib.h>

#include "cache.h"

struct cache cach_info;

void cach_init()
{
    cach_info.head = NULL;
    cach_info.size = 0; 
	return;
}
/* Add body to the head of the cache buffer; 
   Return the node ptr to the place where data is added,
   and NULL on error */
struct node * cach_add(char * host, char * path, 
              struct tm *expire, char *body, int len)
{
	struct node * nd = NULL;	
	struct cache * p = &cach_info;
    if((p->size + MAX_OBJECT_SIZE) > MAX_CACHE_SIZE) //Buffer is full;
        return cach_insert( host, path, expire, body, len);
    
	if((nd = (struct node *) malloc(sizeof(struct node))) == NULL) {
		    printf("WARNING: Malloc failure\n");
			return cach_insert(host, path, expire, body, len);
			
	}
		
	p->size +=  MAX_OBJECT_SIZE;
	
	strcpy(nd->host, host);
	strcpy(nd->path, path);
	strncpy(nd->data, body, len);
	nd->len = len;
    nd->expire = expire;

    // Insert the node to the head of the available list;
 	nd->next = p->head;
    p->head = nd;
	
	return nd;
}

/* Replace the LRU cache copy with body, and make it the new head; 
   called only when the deleted list empty and no space for more nodes; 
   Return the node ptr to the place data is inserted, and NULL on error */
struct node * cach_insert(char *host, char *path, 
                          struct tm *expire, char *body, int len)
{   
	if(cach_info.head == NULL)
		return NULL;

    struct node *p, *prev;
	p = cach_info.head;
	prev = NULL;
	while(p->next != NULL) {
	    prev = p;
		p = p->next;
	}
    
	strcpy(p->host, host);
	strcpy(p->path, path);
	strncpy(p->data, body, len);
	p->expire = expire;
	p->len = len;

	if(prev == NULL) 
		return p;
    
	prev->next = NULL;
    p->next = cach_info.head;
	cach_info.head = p;

	return p; 
}

// Delete the node p from the available list and insert it to the deleted;
void cach_delete(struct node *p, struct node *prev)
{  
	//Delete the node p from the available list;
	if(prev == NULL) //if p is the head of the node list
		cach_info.head = p->next;	
	else
		prev->next = p->next;
		
    free(p);
	return;
}



/* Search the cache copy matching the host and path from the cache buffer, 
   and return the corresponding node pointer; If prev is NULL, ignore it; 
   otherwise fill in its ref. the node addr prev to the returned node;
   Return NULL if not found. */
struct node * cach_search(char *host, char *path, struct node **prev)
{
    if(cach_info.head == NULL)
	    return NULL;
	
	int flag; //Should or not fill the address value of the previous node;
	if(prev == NULL) 
		flag = 0;
	else  
		flag = 1;

    struct node * p;
	p = cach_info.head;
    if(flag)
	    *prev = NULL;

	while(p != NULL) {
	    //If both host names and pathes math;
        if((!strcmp(p->host, host)) && (!strcmp(p->path, path)))	    
		    break;	
		if(flag)
			*prev = p;
		
		p = p->next;
	}	   
	
	return p;    	
}

void cach_free()
{
    struct node *p, *prev;
	p = cach_info.head;
	while(p != NULL) {
		prev = p;
		p = p->next;
		free(prev);
	}	
    cach_info.head = NULL;

    return;
}

