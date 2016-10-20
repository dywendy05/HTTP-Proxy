/*
1. There are 2 lists of (struct) nodes, one of which is called  "valid list" 
   because its nodes contain valid caches, and the other of which is called 
   "invalid list" because its nodes contain invalid caches; 

   Once a valid cache becomes invalid such as when it has expired, we move 
   the node containing this cache from the valid list into front of the 
   invalid list. 

2. Valid list:  we insert into its frond and delete from its end; 
		We record both its head and tail, and each node has both a pointer 
		to the previous node and a pointer to the next node.
   
   Invalid list:  we both insert and delete from its front; We record only 
		its head, and each node has only a pointer to its next node.
*/
#include <stdio.h>
#include <string.h>
#define _XOPEN_SOURCE
#include <time.h>
#include <stdlib.h>

#include "cache.h"

static struct cache valid_list;
static struct cache invalid_list;

void cach_init()
{
    valid_list.head = valid_list.tail = NULL;
    valid_list.size = 0; 

	invalid_list.head = invalid_list.tail = NULL;
	invalid_list.size = 0;
	return;
}
/* Add node containing new cache to the head of the valid list;
   Return the pointer to the node where the new cache is stored on success,
   and NULL on error */
struct node * cach_add(char *host, char *path, struct tm *expire, 
					   char *body, int len)
{
	if(len > MAX_OBJECT_SIZE)
		return NULL;

	struct node * nd;	
	struct cache * vlst = &valid_list;
	struct cache * ilst = &invalid_list;

	// If invalid_list is not empty, cache data to its head node and move 
	// that node into the front of valid_list;
	if(ilst->head != NULL) {
		nd = ilst->head;
		ilst->head = nd->next;
		vlst->size += MAX_OBJECT_SIZE;
	} 
	// If the invalid list is empty and our buffer is not full, allocate 
	// a new node and insert it into the front of the valid list;
	else if(vlst->size < MAX_CACHE_SIZE) {
		if((nd = (struct node *) malloc(sizeof(struct node))) == NULL) {
				printf("WARNING: Malloc failure\n");
				return NULL;
		}
		memset(nd, 0, sizeof(struct node));
		vlst->size += MAX_OBJECT_SIZE;
	} 
	// If buffer is full, remove the LRU node (tail) from the valid list,
	// rewrite the node and then insert it into the front of the valid list;
	// It's a FIFO policy; 
	else {
		nd = vlst->tail;
		struct node *prev = nd->prev;
		prev->next = NULL;
		vlst->tail = prev;
	} 
		
	// Insert node into the front of the valid list;		
	struct node *vhd = vlst->head;
	if(vhd != NULL) 
		vhd->prev = nd;
	else 
		vlst->tail = nd;
	
	nd->next = vhd;
	nd->prev = NULL;
	vlst->head = nd;

	// Store cache and metainforamtion;	
	strcpy(nd->host, host);
	strcpy(nd->path, path);
	strncpy(nd->data, body, len);
	nd->len = len;
	nd->expr_year = expire->tm_year;
	nd->expr_day = expire->tm_yday;
	
	return nd;
}

// Remove the pointed node from the valid list and insert it into
// the front of the invalid list;
void cach_delete(struct node *nd)
{  
	struct node *prev = nd->prev;
	struct node *next = nd->next;
	if(prev != NULL) {
		prev->next = next;
		if(next != NULL)
			next->prev = prev;
		else 
			valid_list.tail = prev;
	}
	else {
		valid_list.head = next;
		if(next != NULL)
			next->prev = NULL;
		else 
			valid_list.tail = NULL;
	}

	// Insert node nd into the front of the invalid list;
	nd->next = invalid_list.head;
	invalid_list.head = nd;

	return;
}

// Search in the valid list for the node that matches the host and path;
// Return the address of the found node on succsse or NULL if not found;
struct node * cach_search(char *host, char *path)
{
    struct node *nd = valid_list.head;
	while(nd != NULL) {
	    //If both host names and pathes math;
        if((!strcmp(nd->host, host)) && (!strcmp(nd->path, path)))	    
		    break;	
		nd = nd->next;
	}	   
	
	return nd;    	
}

void cach_free()
{
    struct node *p, *prev;
	p = valid_list.head;
	while(p != NULL) {
		prev = p;
		p = p->next;
		free(prev);
	}	
	p = invalid_list.head;
	while(p != NULL) {
		prev = p;
		p = p->next;
		free(prev);
	}
    return;
}
