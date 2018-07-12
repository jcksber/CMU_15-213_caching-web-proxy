/*
 * pcache.h 
 * 
 * Made: August 6, 2015 by jkasbeer
 * Version: 1.0
 *
 * Proxy Lab
 *
 * This is the header file for pcache.c (web object cache for proxy)
 */
#ifndef __PCACHE_H__
#define __PCACHE_H__

/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1049000 // 1 Mb
#define MAX_OBJECT_SIZE 102400 // 100 Kb

/* Structure of a cache line consists of an identifier (loc),
 * an age (for LRU), the cached web object, it's size, and 
 * a pointer to the next cache line in the linked list. 
 */
struct cache_line {
  unsigned int size;               
  unsigned int age;                
  char *loc;              
  char *obj;           
  struct cache_line *next; 
}; 
typedef struct cache_line line;

/* Structure of a web cache consists of a pointer to the first
 * line of the cache, the last line of the cache, and the total 
 * size of the cache.  
 * Its main purpose is to maintain the linked list & make 
 * searching the list faster.
 */
struct web_cache {
  unsigned int size;
  line *start;
};
typedef struct web_cache cache;

/* Function prototypes for cache operations */ 
void cache_init(cache *cash, pthread_rwlock_t *lock);
int cache_full(cache *cash);
void cache_free(cache *cash);
/* Function prototypes for cache_line operations */
line *in_cache(cache *cash, char *host, char *path);
line *make_line(char *host, char *path, char *object, size_t obj_size);
void add_line(cache *cash, line *lion);
void remove_line(cache *cash, line *lion);
line *choose_evict(cache *cash);
void free_line(cache *cash, line *lion);;
void age_lines(cache *cash);
/* Function prototypes for debugging */
void cache_error(char *msg);
void print_cache(cache *cash);
void print_line(line *lion);

#endif