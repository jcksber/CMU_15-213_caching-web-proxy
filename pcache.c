/*
 * pcache.c
 *
 * Made: August 6, 2015 by jkasbeer
 * Version: 1.0
 *
 * Proxy Lab 
 *
 * This is the web object cache used for Part 3 of the Proxy Lab; it's 
 * implemented as a linked list with a semi-LRU eviction policy.
 */

#include "csapp.h"
#include "pcache.h"


/*****************
 * CACHE FUNCTIONS
 *****************/

/* Note: malloc for cache outside of init
 * cache_init - initialize shared cache [cash] & read-write locks
 */
void cache_init(cache *cash, pthread_rwlock_t *lock)
{ 
  /* Initialize read-write lock */
  Pthread_rwlock_init(lock, NULL);

  /* Init cache to empty state */
  cash->size = 0;
  cash->start = NULL;
}

/*
 * cache_full - determines if cache [cash] is full;
 *              returns 1 if full, 0 if not
 */
int cache_full(cache *cash)
{
  // The cache is full if there isn't enough room for another object
  return ((MAX_CACHE_SIZE - (cash->size)) < MAX_OBJECT_SIZE);
}

/*
 * cache_free - frees the cache [cash] from memory, including all
 *              of the lines in it (if any)
 */
void cache_free(cache *cash) 
{
  /* Need a ptr to keep track of next so current->next can be freed */
  line *lion = cash->start;
  line *nextlion = lion->next;
  /* Free all the lines in the cache */
  while (lion != NULL) {
    /* Free a line */
    free_line(cash, lion);
    Free(lion->next); 
    lion = nextlion;
    /* Get the next line to be freed */
    if (nextlion != NULL)
      nextlion = nextlion->next;
    else break; // end of cache
  }
  /* Free start of cache */
  Free(cash->start);
}


/**********************
 * CACHE LINE FUNCTIONS
 **********************/

/*
 * in_cache - determines if a web object in question (host/path)
 *            is already in the cache;
 *            returns pointer to line if it is, NULL if it isn't
 */
line *in_cache(cache *cash, char *host, char *path)
{
  size_t hp = strlen(host) + strlen(path) + 2;
  char loc[hp];

  memset(loc, 0, sizeof(loc));

  /* CRITICAL SECTION: READING */ 
  /* Nothing is in the cache if it's empty */
  if (cash->size == 0) return NULL;
  /* Incr. age of lines */
  age_lines(cash);
  /* Create the location given host and path */
  strcat(loc, host);
  strcat(loc, path);

  /* Determine if this object is cached */
  line *object = NULL;
  line *lion = cash->start;
  while (lion != NULL) 
  {
    if (!strcmp(loc, lion->loc)) {
      object = lion;
      break; // Object found!
    }
    lion = lion->next;
  }
  /* END CRITICAL SECTION */

  return object; 
}

/*
 * make_line - create a line that can be inserted into the cache using 
 *             a given hostname [host], path to an object [path], size of
 *             the object [size],and the object as it would be returned 
 *             to the client [object];
 *             returns a pointer to this line
 */
line *make_line(char *host, char *path, char *object, size_t obj_size)
{
  /* Variables to build the elements of the line */
  line *lion;

  size_t loc_size = strlen(host) + strlen(path) + 2;
  char location[loc_size];

  memset(location, 0, sizeof(location));

  /* Allocate space for this line */ 
  lion = Malloc(sizeof(struct cache_line)); 

  /* Set size and age of line */
    lion->size = (unsigned int)obj_size;
    lion->age = 0;

  /* Set the location of the line (identifier) */
  // Combine host & path
    strcat(location, host);
    strcat(location, path);

  // Allocate space for loc
    loc_size = strlen(location) + 1;
    lion->loc = Malloc(loc_size);
  // Finish
    memcpy(lion->loc, location, loc_size);

  /* Set the object of the line (core purpose of line) */
  // Allocate space for obj
    lion->obj = Malloc(obj_size+1);
  // Finish
    memcpy(lion->obj, object, obj_size);

  /* A brand new line is alone in the world until added to cache */
  lion->next = NULL; 

  return lion;
}

/* 
 * add_line - add a line [lion] to the cache and evict if necessary
 *
 * Note: must call make_line before adding a line
 */
void add_line(cache *cash, line *lion) 
{
  /* CRITICAL SECTION: WRITE */
  /* If the cache is full, choose a line to evict & remove it */
  if (cache_full(cash))
    remove_line(cash, choose_evict(cash));
  /* Insert the line at the beginning of the list */
  lion->next = cash->start;
  cash->start = lion;
  /* Update the cache size accordingly */
  cash->size += lion->size;
  /* END CRITICAL SECTION */
}

/*
 * age_lines - age the cache (for LRU policy)
 */
void age_lines(cache *cash)   
{
  line *lion = cash->start;
  /* Increment age of all lines */
  while (lion != NULL) 
  { (lion->age)++; lion = lion->next; }
}

/*
 * remove_line - remove a line [lion] from the cache 
 */
void remove_line(cache *cash, line *lion) 
{
  line *tmp = cash->start;
  /* Case: first line of cache */
  if (tmp == lion) {
  // Adjust start of cache
    cash->start = lion->next;
  // Fully free line
    free_line(cash, lion);
    free(lion);
    return;
  }
  while (tmp != NULL)
  {
    /* Case: middle line of cache */
    if (tmp->next == lion) {
    // Adjust previous line's next ptr
      tmp->next = lion->next;
    // Fully free line
      free_line(cash, lion);
      free(lion);
      return;
    }
    /* Continue */
    else tmp = tmp->next;
  }
  /* Case: line not found.. can't remove */
  cache_error("remove_line error: line not found");
}

/*
 * choose_evict - choose a line to evict using an LRU policy;
 *                return a pointer to the chosen line
 */
line *choose_evict(cache *cash)          
{
  line *evict, *lion;
  int eldest = -1;

  lion = cash->start;
  evict = lion;
  /* Search the cache for the oldest line */
  while (lion != NULL) {
    if (lion->age > eldest) {
      eldest = lion->age;
      evict = lion;
    }
    lion = lion->next;
  }
  return evict;
}

/* 
 * free_line - free a specified line [lion] from cache [cash]
 */
void free_line(cache *cash, line *lion)
{
  /* Before freeing, update cache size */
  cash->size -= lion->size;
  /* Free elements of line (except next--needed for freeing cache) */
  Free(lion->loc);
  Free(lion->obj);
}


/*********************
 * DEBUGGING FUNCTIONS
 *********************/

/*
 * cache_error - signal errors related to internal functions failing
 *               (used as a replacement for unix_error)
 */
void cache_error(char *msg)
{
  fprintf(stderr, "cache_error signaled: %s\n", msg);
}

/* Please ignore these :) */
/*
 * print_cache - print out the cache
 */
void print_cache(cache *cash)
{
  line *lion;

  printf("######## WEB CACHE START ########\n");
  printf("- CACHE STATE -\n");
  printf("Size: %u\n", cash->size);
  if (cash->start) printf("Start: %s\n", (cash->start)->loc);
  else             printf("Start: NULL\n");
  printf("---------------\n\n");

  printf("- CACHE LINES -\n");
  lion = cash->start;
  while (lion != NULL) 
  {
    print_line(lion);
    lion = lion->next;
  }
  printf("---------------\n");
  printf("######### WEB CACHE END #########\n\n");
}

/*
 * print_line - print out a specific line of the cache
 */
void print_line(line *lion)
{
  uint size, age;
  char *location, *object;
  line *next;

  /* Valid line */
  if (lion) {
    /* Parts of a line */
    size     = lion->size;
    age      = lion->age;
    location = lion->loc;
    object   = lion->obj;
    next     = lion->next;
    /* Print this line */
    // Start & size
    printf("[ %u bytes ", size);
    // Location
    if (strlen(location)) printf("| %s ", location);
    else printf("| EMPTY LOC ");
    // Object
    if (strlen(object))   printf("| . . . ", object);
    else printf("| EMPTY OBJ ");
    // Age
    printf("| age=%u ] ", age);
    // Next & end
    if (next) // If it's another line, value=location
      printf("--> [ %s ]\n", next->loc);
    else // If it's not, value=NULL (end of cache?)
      printf("--> NULL\n");
  } 
  /* NULL line */
  else printf("[ NULL LINE ]\n");
  memset(location, 0, strlen(location));
  memset(object, 0, strlen(location));
}
/* End ignore these */



