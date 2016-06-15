myhttpd - A very simple web server
==================================

This is a very simple web server built for NCTU Advanced Unix Programming course.

# What are implemented

- GET a static object
- GET a directory
    - Directory listing
    - HTTP 301 Redirect for directory without trailing slash
- Execute CGI programs

# Build and Run

First, compile *myhttpd* from source,

```bash
$ make
g++ myhttpd.cpp -std=c++11 -Wall -c
g++ myhttpd.o -o myhttpd
```

Now you can specify the **port number** and **ducument root directory** to run *myhttpd*,

```bash
$ ./myhttpd 8080 "/Users/hungys/wwwroot"
```
