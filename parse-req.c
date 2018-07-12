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