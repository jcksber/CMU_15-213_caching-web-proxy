# Introduction to Computer Systems (15-213/18-213)
# Proxy Lab: Writing a Caching Web Proxy
##### Carnegie Mellon University; Nathaniel Filardo

### Author: Jack Kasbeer
### Created: August, 2015

## Lab Assignment
> In this lab, you will write a simple HTTP proxy that caches web objects. For the first part of the lab, you will set up the proxy to accept incoming connections, read and parse requests, forward requests to web servers, read the servers’ responses, and forward those responses to the corresponding clients. This first part will involve learning about basic HTTP operation and how to use sockets to write programs that communicate over network connections. In the second part, you will upgrade your proxy to deal with multiple concurrent connections. This will introduce you to dealing with concurrency, a crucial systems concept. In the third and last part, you will add caching to your proxy using a simple main memory cache of recently accessed web content.

## Overview of Solution 
This is a concurrent web proxy with a 1 MiB web object cache that can handle nearly all HTTP/1.0 GET requests. The cache can handle objects up to 10 KiB in size, and is implemented with a LRU eviction policy. It runs concurrently by creating a new  thread for each new client that makes a request to a server, and features read-write locks for reading & writing concurrency, favoring writers. Tests concluded there was an approximate 5,000% reduction in loading time for sites cached by my proxy.

The web object cache is implemented as a linked list with a semi-LRU eviction policy.

## proxy.c
### Headers Specified
```C
static const char *user_agent_hdr = 
"User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 Firefox/10.0.3\r\n";
static const char *accept_hdr = 
"Accept: text/html,application/xhtml+xml,application/xml;q=0.9,*/*;q=0.8\r\n";
static const char *accept_encoding_hdr = "Accept-Encoding: gzip, deflate\r\n";
static const char *conn_hdr = "Connection: close\r\n";
static const char *pconn_hdr = "Proxy-Connection: close\r\n";
static const char *end_hdr = "\r\n";
static const char *web_port = "80";
```

### Implemented Functions
```C
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
```

### Important Functions Briefly Explained
#### `main(int argc, char **argv)`
`main`: main proxy routine - listens for client requests and creates a new thread to process and forward each one as they come.
```C
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
```

#### `void *thread(void *fd)`
`thread`: connect to the client on the `[fd]` and forward request.  The most important part of this is the conversation of the input from a `void*` to an `int`, eliminating a potentially disastrous race condition.
```C
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
```

#### `void connect_req(int connection)`
`connect_req`: check for errors in the client request, parse the request, open a connection with the server, and finally, forward the request to the server.
```C
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
```

#### `forward_req(int server, int client, rio_t *requio, char *host, char *path)`
This is definitely the beef of the proxy as it does some seriously risky string manipulation that happens to get really messy with memory.  Regardless, it's a beautiful solution :)

`void forward_req(int server, int client, rio_t *requio, 
                 char *host, char *path)`: forward the client's request to the server; use file descriptor server & read client headers from &requio.
```C
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
```

## pcache.c
This is the other most important file as it's the implementation of my personal cache for the proxy.  It's fairly straightforward so if you're interested that's the best place to learn about it.

## Resources 
* CS:APP package:
  * Robust I/O (RIO) package (in contrast to POSIX)
  * POSIX Threads (Pthreads) library
* Shark machines: rack-mounted Intel Nehalem-based servers [Tech Specs](https://www.cs.cmu.edu/~213/labmachines.html)
* Carnegie Mellon Staff & Students
