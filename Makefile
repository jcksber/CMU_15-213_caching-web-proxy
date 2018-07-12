#
# Makefile for Proxy Lab 
#
# You may modify is file any way you like (except for the handin
# rule). Autolab will execute the command "make" on your specific 
# Makefile to build your proxy from sources.
#
CC = gcc
CFLAGS = -Wall -Wextra -Werror -o2 -gdwarf-2 -std=gnu99
CSFlags = -g -Wall 
LDFLAGS = -lpthread

all: proxy 

csapp.o: csapp.c csapp.h
	$(CC) $(CSFLAGS) -c csapp.c
pcache.o: pcache.c pcache.h
	$(CC) $(CSFLAGS) -c pcache.c
proxy.o: proxy.c csapp.h
	$(CC) $(CSFLAGS) -c proxy.c

proxy: pcache.o proxy.o csapp.o 

# Creates a tarball in ../proxylab-handin.tar that you should then
# hand in to Autolab. DO NOT MODIFY THIS!
handin:
	(make clean; cd ..; tar cvf proxylab-handin.tar proxylab-handout --exclude tiny --exclude nop-server.py --exclude proxy --exclude driver.sh --exclude port-for-user.pl --exclude free-port.sh --exclude ".*")

clean:
	rm -f *~ *.o proxy core *.tar *.zip *.gzip *.bzip *.gz

