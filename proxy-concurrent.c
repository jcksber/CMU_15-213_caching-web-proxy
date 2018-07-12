/*
 * proxy.c
 *
 * Made: August 5, 2015 by jkasbeer
 * Version: Part 2
 *
 * Proxy Lab
 * 
 * This is a concurrent web proxy server that's implemented for 
 * HTTP/1.0 server requests.  In contrast to the sequential implementation
 * from Part 1, this version creates a new thread each time a client
 * sends a request to a server.  Hence, it can handle multiple requests at once.
 */

#include <stdio.h>
#include "csapp.h"

/* String constant macros */
#define MAXPORT    8 // max port length (no larger than 6 digits)

/* Useful for debugging macros */
#define CHKPT "nigga we made it\n"

/* Global var's */
static const char *user_agent_hdr = "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 Firefox/10.0.3\r\n";
static const char *accept_hdr = "Accept: text/html,application/xhtml+xml,application/xml;q=0.9,*/*;q=0.8\r\n";
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
void forward_req(int server, int client, rio_t *requio,
                 char *host, char *path);

/* Error handling functions */
void check_argc(int argc, int check, char **argv);

void bad_method(int fd, char *cause);
void bad_request(int fd, char *cause);
void not_found(int fd, char *cause);

void flush_str(char *str);
void flush_strs(char *str1, char *str2, char *str3);

/*
 * main - main proxy routine: listens for client requests
 *        and creates a new thread to process and forward 
 *        each one as they come.
 */
int main(int argc, char **argv)
{
  /* Check command line args */
  check_argc(argc, 2, argv);

  /* Main routine variables */
  int plisten, *connection;      // File descriptors
  struct sockaddr_storage caddr; // Client info
  socklen_t clen;

  /* Listen on port specified by user */
  plisten = Open_listenfd(argv[1]); 
  clen = sizeof(caddr);
  pthread_t tid; // Thread 

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
  /* Core var's of sconnect */                                     
  int middleman;                                    // File descriptor
  char host[MAXLINE], port[MAXPORT], path[MAXLINE]; // Server info
  /* Rio to parse client request */
  rio_t rio;                                        

  /* Parse client request into host, port, and path */
  if (parse_req(connection, &rio, host, port, path) < 0) {
    if (!strlen(host)) not_found(connection, host);
    if (!strlen(port)) not_found(connection, port);
    if (!strlen(path)) not_found(connection, path);
    flush_strs(host, port, path);
  }

  /* Connect to server running on [host] and listening
     for connection requests on [port] */
  if ((middleman = Open_clientfd(host, port)) < 0) {
    bad_request(middleman, host);
    flush_strs(host, port, path);
  }

  /* Forward request to server */                              
  forward_req(middleman, connection, &rio, host, path);

  /* Clean up & close connection to server */
  flush_strs(host, port, path); 
  Close(middleman);
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
  char meth[MAXLINE],   // 3 parts of a request
       uri[MAXLINE],          
       vers[MAXLINE];   
  char rbuf[MAXLINE];   // placeholder
  /* Strings to keep track of uri parsing */
  char *spec;           // port specified ?
  char *buf, *p, *save; // used for explicit uri parse
  /* Constants necessary for string lib functions */
  const char colon[2] = ":";
  const char bslash[2] = "/"; 

  /* SETUP FOR PARSING -- */
    /* Initialize rio & parse request into useful fields */
    Rio_readinitb(rio, connection); 

    /* Error: Rio_readlineb failed.. */
    if (!Rio_readlineb(rio, rbuf, MAXLINE)) {
      bad_request(connection, rbuf);
      flush_str(rbuf);
      return -1;
    } 
    /* Splice the request */
    sscanf(rbuf, "%s %s %s", meth, uri, vers);

    printf("%s", rbuf); // For debugging, print the client request
    flush_str(rbuf); // Don't need this buffer anymore
    
    /* Error: HTTP request that isn't GET */
    if (strcmp(meth, "GET")) {                   
      bad_method(connection, meth);
      flush_strs(meth, uri, vers);
      return -1;
    } 
    /* Error: 'http://' not found in uri */ 
    if (!(strstr(uri, "http://"))) {
      bad_request(connection, uri);
      flush_strs(meth, uri, vers);
      return -1;
    }
  /* -------------------- */

  /* Parse uri */
  else {
    buf = uri + 7;          // Set up buf to ignore 'http://' 
    spec = index(buf, ':'); // Determine if port specified
  /* PORT IS SPECIFIED -- */
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
      while ((p = strtok_r(NULL, bslash, &save)) != NULL) {
        strcat(path, bslash);
        strcat(path, p);
      }
    } 
  /* -------------------- */
  /* PORT IS NOT SPECIFIED -- */
    else { 
    // Get host name
      p = strtok_r(buf, bslash, &save);  
      strcpy(host, p);
    // Get path
      while ((p = strtok_r(NULL, bslash, &save)) != NULL) {
        strcat(path, bslash);
        strcat(path, p);
      }
    // Set port as unspecified
      strcpy(port, web_port);
    }
  /* ------------------------ */
  /* Clean-up & return success */
    flush_strs(meth, uri, vers);
    flush_strs(spec, buf, p);
    flush_str(save);
    return 0;
  }
}

/*
 * forward_request - Forward the client's request to the server;
 *                   use file descriptor server_fd & read client headers
 *                   from &requio
 */
void forward_req(int server, int client, rio_t *requio, 
                 char *host, char *path) 
{
  /* Client-side reading */
  char buf[MAXLINE];   // for building http GET request
  char cbuf[MAXLINE];  // buffer for reading (client)
  ssize_t n = 0;       // size of client-side read line (bytes)

  /* Server-side reading */
  char svbuf[MAXLINE]; // buffer for reading (server)
  rio_t respio;        // rio for server-side reading
  ssize_t m = 0;       // size of server-side read line (bytes)

  /* BUILD & FORWARD REQUEST TO SERVER -- */
    /* Build first line of request (GET) */
    sprintf(buf, "%sGET %s HTTP/1.0\r\n", buf, path);
    /* Build client headers */
    n = Rio_readlineb(requio, cbuf, MAXLINE);
    // first header
    sprintf(buf, "%s%s", buf, cbuf); 
    while(strcmp(cbuf, "\r\n") && n != 0) 
    { // rest of headers   
      if (strstr(cbuf, "Host")) 
      { // If host specified, change the hostname
        host = cbuf + 6; // point to string after 'Host: '
        break;           // don't add to buf
      }
      n = Rio_readlineb(requio, cbuf, MAXLINE);
      // Don't print empty line from client!
      if (!strcmp(cbuf, "\r\n"))
        sprintf(buf, "%s%s\r\n", buf, cbuf);
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
    Rio_writen(server, buf, strlen(buf));
  /* ------------------------------------ */

  /* BUILD & FORWARD SERVER RESPONSE TO CLIENT -- */ 
    /* Initialize rio to read server's response */          
    Rio_readinitb(&respio, server);
    /* Read from fd [server] & write to fd [client] */
    while ((m = Rio_readlineb(&respio, svbuf, MAXLINE)) != 0)
    { 
      Rio_writen(client, svbuf, m);
    }
  /* -------------------------------------------- */

  /* Clean-up */
  flush_strs(buf, cbuf, svbuf);
  flush_strs(host, path, NULL);
}


/********************
 * CLEAN-UP FUNCTIONS
 ********************/

/*
 * flush_str - wipe memory of a single string
 */
void flush_str(char *str)
{
  if (str) *str = '\0';
}
/*
 * flush_strs - wipe memory of up to 3 strings 
 */
void flush_strs(char *str1, char *str2, char *str3)
{
  if (str1) *str1 = '\0';
  if (str2) *str2 = '\0';
  if (str3) *str3 = '\0';
}


/****************
 * ERROR HANDLERS
 ****************/

/* Error: 501
 * bad_method - client error or incomplete proxy error:
 *              method is not supported and/or doesn't exist
 */
void bad_method(int fd, char *cause)
{
  char buf[MAXLINE];

  sprintf(buf, "<html><title>Error 501: method not supported</tite>\r\n");
  sprintf(buf, "%s<body><p>Caused by %s</p></body></html>\r\n", buf, cause);

  Rio_writen(fd, buf, strlen(buf));
}

/* Error: 400
 * bad_request - client error: invalid format in request
 */
void bad_request(int fd, char *cause)
{
  char buf[MAXLINE];

  sprintf(buf, "<html><title>Error 400: bad request</tite>\r\n");
  sprintf(buf, "%s<body><p>Caused by %s</p></body></html>\r\n", buf, cause);

  Rio_writen(fd, buf, strlen(buf));
}

/* Error: 404
 * bad_req - file not found
 */
void not_found(int fd, char *cause)
{
  char buf[MAXLINE];

  sprintf(buf, "<html><title>Error 404: file not found</tite>\r\n");
  sprintf(buf, "%s<body><p>Caused by %s</p></body></html>\r\n", buf, cause);

  Rio_writen(fd, buf, strlen(buf));
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

/**********
 * WRAPPERS
 **********/


















