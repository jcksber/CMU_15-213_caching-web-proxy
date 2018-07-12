# Introduction to Computer Systems (15-213/18-213)
# Proxy Lab: Writing a Caching Web Proxy
##### Carnegie Mellon University; Nathaniel Filardo

### Author: Jack Kasbeer
### Created: August, 2015

## Lab Assignment
> In this lab, you will write a simple HTTP proxy that caches web objects. For the first part of the lab, you will set up the proxy to accept incoming connections, read and parse requests, forward requests to web servers, read the servers’ responses, and forward those responses to the corresponding clients. This first part will involve learning about basic HTTP operation and how to use sockets to write programs that communicate over network connections. In the second part, you will upgrade your proxy to deal with multiple concurrent connections. This will introduce you to dealing with concurrency, a crucial systems concept. In the third and last part, you will add caching to your proxy using a simple main memory cache of recently accessed web content.

## Overview of Solution 
This is a concurrent web proxy with a 1 MiB web object cache that can handle nearly all HTTP/1.0 GET requests. The cache can handle objects up to 10 KiB in size, and is implemented with a LRU eviction policy. It runs concurrently by creating a new  thread for each new client that makes a request to a server, and features read-write locks for reading & writing concurrency, > favoring writers. Tests concluded there was an approximate 5,000% reduction in loading time for sites cached by my proxy.

## Headers Specified
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

## Resources 
* CS:APP package:
  * Robust I/O (RIO) package (in contrast to POSIX)
  * POSIX Threads (Pthreads) library
* Shark machines: rack-mounted Intel Nehalem-based servers [Tech Specs](https://www.cs.cmu.edu/~213/labmachines.html)
* Carnegie Mellon Staff & Students
