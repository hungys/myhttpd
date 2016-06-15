UNAME := $(shell uname)
CC = g++

ifeq ($(UNAME), Darwin)
	CFLAGS = -stdlib=libc++ -std=c++11 -Wall
else
	CFLAGS = -std=c++11 -Wall
endif


hw4: myhttpd.o
	$(CC) myhttpd.o -o myhttpd
myhttpd.o: myhttpd.cpp
	$(CC) myhttpd.cpp $(CFLAGS) -c
clean:
	rm -rf myhttpd.o myhttpd
