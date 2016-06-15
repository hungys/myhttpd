CC = g++
CFLAGS = -std=c++11 -Wall

hw4: myhttpd.o
	$(CC) myhttpd.o -o myhttpd
myhttpd.o: myhttpd.cpp
	$(CC) myhttpd.cpp $(CFLAGS) -c
clean:
	rm -rf myhttpd.o myhttpd
