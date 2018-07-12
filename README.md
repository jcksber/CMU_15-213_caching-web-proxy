# Introduction to Computer Systems (15-213/18-213)
# Proxy Lab: Writing a Caching Web Proxy
##### Carnegie Mellon University; Nathaniel Filardo

### Author: Jack Kasbeer
### Created: August, 2015

## Overview
This is a concurrent web proxy with a 1 MiB web object cache that can handle nearly all HTTP/1.0 GET requests. The cache can handle objects up to 10 KiB in size, and is implemented with a LRU eviction policy. It runs concurrently by creating a new thread for each new client that makes a request to a server, and features read-write locks for reading & writing concurrency, favoring writers. Tests concluded there was an approximate 5,000% reduction in loading time for sites cached by my proxy.

## Resources 
* Carnegie Mellon Staff & Students
* CS:APP package:
  * Robust I/O (RIO) package (in contrast to POSIX)
  * POSIX Threads (Pthreads) library
