/*
 * proxy.c
 *
 * Made: July 31, 2015 by jkasbeer
 * Version: Part 3
 * Known bugs: (1) Aborts in Mozilla Firefox @ www.cmu.edu with an error in
 *             Getaddrinfo. Stems from call to Open_clientfd & fails 
 *             while executing freeaddrinfo (run-time error).
 *
 * Proxy Lab
 * 
 * This is a concurrent web proxy with a 1 MiB web object cache.
 * The cache can handle objects up to 10 KiB in size, and is implemented 
 * with a LRU eviction policy. It runs concurrently by creating a new thread 
 * for each new client that makes a request to a server, and features 
 * read-write locks for concurrent cache reading & writing, favoring writers.  
 * This was my favorite lab and I'm beyond proud of what I've written.
 */

#include <stdio.h>
#include "csapp.h"
#include "pcache.h"

/* String constant macros */
#define MAXPORT    8 // max port length (no larger than 6 digits)
#define BIGBUF 16384 // max buf length (16 Kb)

/* Global var's */
static const char *user_agent_hdr = 
"User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 Firefox/10.0.3\r\n";
static const char *accept_hdr = 
"Accept: text/html,application/xhtml+xml,application/xml;q=0.9,*/*;q=0.8\r\n";
static const char *accept_encoding_hdr = "Accept-Encoding: gzip, deflate\r\n";
static const char *conn_hdr = "Connection: close\r\n";
static const char *pconn_hdr = "Proxy-Connection: close\r\n";
static const char *end_hdr = "\r\n";
static const char *web_port = "80";

/* Request handling functions */
void *thread(void *fd);
void connect_req(int connected_fd);
int parse_req(int connection, rio_t *rio, 
              char *host, char *port, char *path);
int is_dir(char *path);
int not_error(char *obj);
void forward_req(int server, int client, rio_t *requio,
                 char *host, char *path);
int ignore_hdr(char *hdr);

/* Error handling functions */
void check_argc(int argc, int check, char **argv);

void bad_request(int fd, char *cause);

void flush_str(char *str);
void flush_strs(char *str1, char *str2, char *str3);

/* Function prototypes for wrapper functions */
int Pthread_rwlock_init(pthread_rwlock_t *rwlock, 
                       const pthread_rwlockattr_t *attr);
int Pthread_rwlock_wrlock(pthread_rwlock_t *rwlock);
int Pthread_rwlock_rdlock(pthread_rwlock_t *rwlock);
int Pthread_rwlock_unlock(pthread_rwlock_t *rwlock);
int Pthread_rwlock_destroy(pthread_rwlock_t *rwlock);

/* Global web cache */
cache *C;   
pthread_rwlock_t lock;

/*
 * main - main proxy routine: listens for client requests
 *        and creates a new thread to process and forward 
 *        each one as they come.
 */
int main(int argc, char **argv)
{
  /* Main routine variables */
  int plisten, *connection;      // File descriptors
  struct sockaddr_storage caddr; // Client info
  socklen_t clen;
  pthread_t tid;                 // Thread 

  /* Some setup.. */
  check_argc(argc, 2, argv);
  C = Malloc(sizeof(struct web_cache));
  cache_init(C, &lock);
  Signal(SIGPIPE, SIG_IGN);

  /* Listen on port specified by user */
  plisten = Open_listenfd(argv[1]);
  clen = sizeof(caddr);

  /* Infinite proxy loop */
  while (1) {
  /* Wait for client to send request */
    connection = Malloc(sizeof(int));
    *connection = Accept(plisten, (SA *)&caddr, &clen);
  /* Create new thread to process the request */
    Pthread_create(&tid, NULL, thread, connection);           
  }
}

/*
 * thread - connect to the client on [fd] & forward request
 */
void *thread(void *fd) 
{
  /* Dereference fd then free it */
  int connection = *((int *) fd);  
  Free(fd); 
  /* Detach thread to avoid memory leaks */
  Pthread_detach(pthread_self()); 
  /* Attempt to connect to server */
  connect_req(connection);    
  /* Close thread's connection to client */
  Close(connection);  
}


/********************
 * MY HELPER ROUTINES
 ********************/

/*
 * sconnect - check for errors in the client request, parse the request,
 *            open a connection with the server, and finally forward 
 *            the request to the server.
 */
void connect_req(int connection)                
{ 
  /* Core var's of connect_req */                                     
  int middleman;            // File descriptor
  char host[MAXLINE] = {0}, // Server info
       port[MAXPORT] = {0}, 
       path[MAXLINE] = {0};       
  /* Rio to parse client request */
  rio_t rio;                                        

  /* Parse client request into host, port, and path */
  if (parse_req(connection, &rio, host, port, path) < 0) {
    fprintf(stderr, "Cannot read this request path..\n");
    flush_strs(host, port, path);
  }
  /* Parsing succeeded.. continue */
  else {
    /* READING */
    Pthread_rwlock_rdlock(&lock);
    line *lion = in_cache(C, host, path);
    Pthread_rwlock_unlock(&lock);
    /* If in cache, don't connect to server */
    if (lion != NULL) {
      if (rio_writen(connection, lion->obj, lion->size) < 0)
        fprintf(stderr, "rio_writen error: bad connection");
      flush_strs(host, port, path);
    }
    /* Otherwise, connect to server & forward request */
    else {
      if ((middleman = Open_clientfd(host, port)) < 0) {
        bad_request(middleman, host);
        flush_strs(host, port, path);
      } 
      else {
        forward_req(middleman, connection, &rio, host, path);
        /* Clean up & close connection to server */
        flush_strs(host, port, path); 
        Close(middleman);
      }
    }
  }
}

/*
 * parse_req - parse client request into method, uri, and version,
 *             then parse the uri into host, port (if specified), and path;
 *             returns -1 on error, 0 otherwise.
 */
int parse_req(int connection, rio_t *rio, 
              char *host, char *port, char *path)  
{  
  /* Parse request into method, uri, and version */
  char meth[MAXLINE] = {0},
       uri[MAXLINE]  = {0},          
       vers[MAXLINE] = {0};   
  char rbuf[MAXLINE] = {0};
  /* Strings to keep track of uri parsing */
  char *spec, *check;           // port specified ?
  char *buf, *p, *save; // used for explicit uri parse
  /* Constants necessary for string lib functions */
  const char colon[2] = ":";
  const char bslash[2] = "/"; 

  /* SETUP FOR PARSING -- */
  /* Initialize rio */
  Rio_readinitb(rio, connection); 
  if (Rio_readlineb(rio, rbuf, MAXLINE) <= 0) {
    bad_request(connection, rbuf);
    flush_str(rbuf);
    return -1;
  } 
  /* Splice the request */
  sscanf(rbuf, "%s %s %s", meth, uri, vers);
  flush_str(rbuf);  
  /* Error: HTTP request that isn't GET or 'http://' not found */
  if (strcmp(meth, "GET") || !(strstr(uri, "http://"))) {                   
    bad_request(connection, uri);
    flush_strs(meth, uri, vers);
    return -1;
  } 
  /* PARSE URI */
  else {
    buf = uri + 7; // ignore 'http://' 
    spec = index(buf, ':'); 
    check = rindex(buf, ':');
    if (spec != check) return -1; // cannot handle ':' after port
  /* Port is specified.. */
    if (spec) {  
    // Get host name
      p = strtok_r(buf, colon, &save);
      // Copy if successful
      strcpy(host, p); 
    // Get port from buf
      buf = strtok_r(NULL, colon, &save);
      p = strtok_r(buf, bslash, &save);
      // Copy if successful
      strcpy(port, p);
    // Get path (rest of buf)
      while ((p = strtok_r(NULL, bslash, &save)) != NULL) 
      { strcat(path, bslash); strcat(path, p); }
      if (is_dir(path)) 
        strcat(path, bslash);
    }
  /* Port not specified.. */
    else { 
    // Get host name
      p = strtok_r(buf, bslash, &save);  
      strcpy(host, p);
    // Get path
      while ((p = strtok_r(NULL, bslash, &save)) != NULL) 
      { strcat(path, bslash); strcat(path, p); } 
      if (is_dir(path)) // append '/' if path is a directory
        strcat(path, bslash);
    // Set port as unspecified
      strcpy(port, web_port);
    }
  /* Clean-up */
    flush_strs(meth, uri, vers);
    flush_strs(spec, buf, p);
    flush_str(save);
    return 0;
  }
}

/*
 * is_dir - determine if path is a directory or contains a file name;
 *          returns 1 if directory, 0 if not 
 */
int is_dir(char *path) 
{
  /* If path is a file we support, return 0 */
  if (strstr(path,".html") || strstr(path,".css") || strstr(path,".xml"))
    return 0;
  else if (strstr(path,".gif") || strstr(path,".png") || strstr(path,".jpg"))
    return 0;
  else if (strstr(path,".c") || strstr(path,".js") || strstr(path,".json"))
    return 0;
  else if (strstr(path,".ini") || strstr(path,".csv") || strstr(path,".tsv"))
    return 0;
  else if (strstr(path,".bak") || strstr(path,".bk") || strstr(path,".bin"))
    return 0;
  else if (strstr(path,".dat") || strstr(path,".dsk") || strstr(path,".raw"))
    return 0;
  else if (strstr(path,".asc") || strstr(path,".txt") || strstr(path,"tiny"))
    return 0;
  else if (strstr(path,".ttf") || strstr(path,".woff"))
    return 0;
  /* Otherwise, path is a directory */
  else
    return 1; 
}  

/*
 * forward_request - Forward the client's request to the server;
 *                   use file descriptor server & read client headers
 *                   from &requio
 */
void forward_req(int server, int client, rio_t *requio, 
                 char *host, char *path) 
{
  /* Client-side reading */
  char buf[BIGBUF] = {0}; 
  char cbuf[MAXLINE] = {0};    
  ssize_t n = 0;               
  /* Server-side reading */
  char svbuf[MAXLINE] = {0}; 
  rio_t respio;              
  ssize_t m = 0;             
  /* Implementing web object cache */
  char object[MAX_OBJECT_SIZE] = {0};  
  size_t obj_size = 0;

  /* BUILD & FORWARD REQUEST TO SERVER -- */
  sprintf(buf, "GET %s HTTP/1.0\r\n", path);
  /* Build client headers */
  while((n = rio_readlineb(requio, cbuf, MAXLINE)) != 0) 
  { 
    if (n < 0) {
      flush_str(cbuf); return;
    }
    if (!strcmp(cbuf, "\r\n")) 
      break; // empty line found => end of headers
    if (!ignore_hdr(cbuf))
      sprintf(buf, "%s%s\r\n", buf, cbuf);
    flush_str(cbuf); // flush line after copied
  }      
  /* Build proxy headers */
  sprintf(buf, "%sHost: %s\r\n", buf, host);
  sprintf(buf, "%s%s", buf, user_agent_hdr);
  sprintf(buf, "%s%s", buf, accept_hdr);
  sprintf(buf, "%s%s", buf, accept_encoding_hdr);
  sprintf(buf, "%s%s", buf, conn_hdr);
  sprintf(buf, "%s%s", buf, pconn_hdr);
  sprintf(buf, "%s%s", buf, end_hdr); 
  /* Forward request to server */
  if (rio_writen(server, buf, strlen(buf)) < 0) {
    flush_str(buf); return;
  }

  /* BUILD & FORWARD SERVER RESPONSE TO CLIENT -- */ 
  /* Initialize rio to read server's response */          
  Rio_readinitb(&respio, server);
  /* Read from fd [server] & write to fd [client] */
  while ((m = Rio_readlineb(&respio, svbuf, MAXLINE)) != 0)
  { 
  // Rio error check
    if (m < 0) {
      flush_str(svbuf); return; 
    }
  // For cache
    obj_size += m; 
    sprintf(object, "%s%s", object, svbuf); 
  // Write to client
    if (rio_writen(client, svbuf, m) < 0) {
      flush_str(svbuf); return;
    }
    flush_str(svbuf);
  }
  /* Object is not cached.
     If it's small enough & not a sever error, cache it */
  if (obj_size <= MAX_OBJECT_SIZE && not_error(object)) {
    /* WRITING */
    Pthread_rwlock_wrlock(&lock);
    add_line(C, make_line(host, path, object, obj_size));
    Pthread_rwlock_unlock(&lock);
  }
  /* Clean-up */
  flush_strs(buf, cbuf, svbuf);
  flush_strs(host, path, object);
}

/*
 * ignore_hdr - if this header is one of the mandatory proxy headers,
 *              ignore it (return 1); if it isn't, don't ignore (return 0)
 */
int ignore_hdr(char *hdr)
{
  // Ignore header for Proxy-Connection
  if (strstr(hdr, "Proxy-Connection"))
    return 1; // ignore
  // Ignore header for Connection
  else if (strstr(hdr, "Connection"))
    return 1; // ignore
  // Ignore empty line for client headers
  else if (strcmp(hdr, "\r\n"))
    return 1; // ignore
  // Everything else is acceptable
  else 
    return 0; // don't ignore
}

/*
 * not_error - determines if obj is a server error or not;
 *             return 1 if it isn't, 0 if it is
 */
int not_error(char *obj)
{
  size_t objsize = strlen(obj) + 1;
  char object[objsize];

  memset(object, 0, sizeof(object));

  memcpy(object, obj, objsize);
  return strstr(object, "200") != NULL ? 1 : 0;
}


/********************
 * CLEAN-UP FUNCTIONS
 ********************/

/*
 * flush_str - wipe memory of a single string
 */
void flush_str(char *str)
{ if (str) memset(str, 0, sizeof(str)); }
/*
 * flush_strs - wipe memory of up to 3 strings 
 */
void flush_strs(char *str1, char *str2, char *str3)
{
  if (str1) memset(str1, 0, sizeof(str1));
  if (str2) memset(str2, 0, sizeof(str2));
  if (str3) memset(str3, 0, sizeof(str3));
}


/****************
 * ERROR HANDLERS
 ****************/

/* Error: 400
 * bad_request - client error: invalid format in request
 */
void bad_request(int fd, char *cause)
{
  char buf[MAXLINE];

  sprintf(buf, "<html><title>Error 400: bad request</title>\r\n");
  sprintf(buf, "%s<body><h1>Error 400: bad request</h1>\r\n", buf);
  sprintf(buf, "%s<h3>Caused by <em>%s</em></h3></body></html>\r\n", buf, cause);

  flush_str(buf);
}

/*
 * check_argc - checks for incorrect argument count to command prompt;
 *              eliminates clutter in main function
 */
void check_argc(int argc, int check, char **argv)
{
  if (argc != check) {
    fprintf(stderr, "usage: %s <port>\n", argv[0]);
    exit(1);
  }
}


/**********************************
 * Wrappers for robust I/O routines
 **********************************/

ssize_t Rio_readn(int fd, void *ptr, size_t nbytes) 
{
  ssize_t n;
  
  if ((n = rio_readn(fd, ptr, nbytes)) != 0) {
    if (errno == ECONNRESET)
      fprintf(stderr, "ECONNRESET muffled by proxy\n");
    else if (n < 0)
      fprintf(stderr, "Rio_readn error\n");
    else
      fprintf(stderr, "Rio_readn returned unknown error\n");
  }

  return n;
}

void Rio_writen(int fd, void *usrbuf, size_t n) 
{
  ssize_t rc;
  if ((rc = rio_writen(fd, usrbuf, n)) != 0) {
    if (errno == EPIPE)
      fprintf(stderr, "EPIPE muffled by proxy\n");
    if (n < 0)
      fprintf(stderr, "Rio_writen error\n");

    flush_str(usrbuf);
  }
}

void Rio_readinitb(rio_t *rp, int fd)
{
  rio_readinitb(rp, fd);
} 

ssize_t Rio_readnb(rio_t *rp, void *usrbuf, size_t n) 
{
  ssize_t rc;

  if ((rc = rio_readnb(rp, usrbuf, n)) < 0)
    fprintf(stderr, "Rio_readnb error\n");

  return rc;
}

ssize_t Rio_readlineb(rio_t *rp, void *usrbuf, size_t maxlen) 
{
  ssize_t rc;

  if ((rc = rio_readlineb(rp, usrbuf, maxlen)) < 0)
    fprintf(stderr, "Rio_readlineb error\n");
  return rc;
} 


/**************************
 * READ-WRITE LOCK WRAPPERS
 **************************/

/*
 * Pthread_rwlock_init - wrapper function for pthread_rwlock_init;
 *                       initializes rwlock
 */
int Pthread_rwlock_init(pthread_rwlock_t *rwlock, 
                       const pthread_rwlockattr_t *attr)
{
  int r;
  if ((r = pthread_rwlock_init(rwlock, attr)) != 0)
    cache_error("pthread_rwlock_init failed");

  return r;
}

/*
 * Pthread_rwlock_wrlock - wrapper function for pthread_rwlock_wrlock;
 *                         applies a write-lock to *rwlock
 */
int Pthread_rwlock_wrlock(pthread_rwlock_t *rwlock)
{
  int r;
  if ((r = pthread_rwlock_wrlock(rwlock)) != 0)
    cache_error("pthread_rwlock_wrlock failed");

  return r;
}

/*
 * Pthread_rwlock_rdlock - wrapper function for pthread_rwlock_rdlock;
 *                         applies a read-lock to *rwlock
 */
int Pthread_rwlock_rdlock(pthread_rwlock_t *rwlock)
{
  int r;
  if ((r = pthread_rwlock_rdlock(rwlock)) != 0)
    cache_error("pthread_rwlock_rdlock failed");

  return r;
}

/*
 * Pthread_rwlock_unlock - wrapper function for pthread_rwlock_unlock;
 *                         unlocks rwlock
 */
int Pthread_rwlock_unlock(pthread_rwlock_t *rwlock)
{
  int r;
  if ((r = pthread_rwlock_unlock(rwlock)) != 0)
    cache_error("pthread_rwlock_unlock failed");

  return r;
}

/*
 * Pthread_rwlock_destroy - wrapper function for pthread_rwlock_destroy;
 *                          destroys read-write lock rwlock
 */
int Pthread_rwlock_destroy(pthread_rwlock_t *rwlock)
{
  int r;
  if ((r = pthread_rwlock_destroy(rwlock)) != 0)
    cache_error("pthread_rwlock_destroy failed");

  return r;
}


