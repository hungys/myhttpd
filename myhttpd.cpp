#include <iostream>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sstream>
#include <map>

#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <arpa/inet.h>

#define DEFAULT_MAX_CLIENTS 1024
#define BUF_SIZE 4096

#define HTTP_PREFIX "http://"
#define CRLF "\r\n"

using namespace std;

const string DEFAULT_PAGES[4] = {"index.html", "index.htm", "default.html", "default.htm"};

const char HEX2DEC[256] =
{
    /*       0  1  2  3   4  5  6  7   8  9  A  B   C  D  E  F */
    /* 0 */ -1,-1,-1,-1, -1,-1,-1,-1, -1,-1,-1,-1, -1,-1,-1,-1,
    /* 1 */ -1,-1,-1,-1, -1,-1,-1,-1, -1,-1,-1,-1, -1,-1,-1,-1,
    /* 2 */ -1,-1,-1,-1, -1,-1,-1,-1, -1,-1,-1,-1, -1,-1,-1,-1,
    /* 3 */  0, 1, 2, 3,  4, 5, 6, 7,  8, 9,-1,-1, -1,-1,-1,-1,

    /* 4 */ -1,10,11,12, 13,14,15,-1, -1,-1,-1,-1, -1,-1,-1,-1,
    /* 5 */ -1,-1,-1,-1, -1,-1,-1,-1, -1,-1,-1,-1, -1,-1,-1,-1,
    /* 6 */ -1,10,11,12, 13,14,15,-1, -1,-1,-1,-1, -1,-1,-1,-1,
    /* 7 */ -1,-1,-1,-1, -1,-1,-1,-1, -1,-1,-1,-1, -1,-1,-1,-1,

    /* 8 */ -1,-1,-1,-1, -1,-1,-1,-1, -1,-1,-1,-1, -1,-1,-1,-1,
    /* 9 */ -1,-1,-1,-1, -1,-1,-1,-1, -1,-1,-1,-1, -1,-1,-1,-1,
    /* A */ -1,-1,-1,-1, -1,-1,-1,-1, -1,-1,-1,-1, -1,-1,-1,-1,
    /* B */ -1,-1,-1,-1, -1,-1,-1,-1, -1,-1,-1,-1, -1,-1,-1,-1,

    /* C */ -1,-1,-1,-1, -1,-1,-1,-1, -1,-1,-1,-1, -1,-1,-1,-1,
    /* D */ -1,-1,-1,-1, -1,-1,-1,-1, -1,-1,-1,-1, -1,-1,-1,-1,
    /* E */ -1,-1,-1,-1, -1,-1,-1,-1, -1,-1,-1,-1, -1,-1,-1,-1,
    /* F */ -1,-1,-1,-1, -1,-1,-1,-1, -1,-1,-1,-1, -1,-1,-1,-1
};

const char http_error_403_page[] =
"<html>" CRLF
"<head><title>403 Forbidden</title></head>" CRLF
"<body bgcolor=\"white\">" CRLF
"<center><h1>403 Forbidden</h1></center>" CRLF
;

const char http_error_404_page[] =
"<html>" CRLF
"<head><title>404 Not Found</title></head>" CRLF
"<body bgcolor=\"white\">" CRLF
"<center><h1>404 Not Found</h1></center>" CRLF
;

const char http_error_default_page[] =
"<html>" CRLF
"<head><title>Unknown Error</title></head>" CRLF
"<body bgcolor=\"white\">" CRLF
"<center><h1>Unknown Error</h1></center>" CRLF
;

const char directory_output_template[] =
"<html>" CRLF
"<head>" CRLF
"<meta http-equiv=\"content-type\" content=\"text/html; charset=utf-8\"/>" CRLF
"<title>{{title}}</title>" CRLF
"<style>" CRLF
"p {" CRLF
"    font-family: monospace;" CRLF
"    white-space: pre;" CRLF
"}" CRLF
"</style>" CRLF
"</head>" CRLF
"<body bgcolor=\"white\">" CRLF
"<h1>Index of {{path}}</h1>" CRLF
"<hr/>" CRLF
"<p>" CRLF
"{{list}}" CRLF
"</p>" CRLF
"<hr/>" CRLF
"</body>" CRLF
"</html>" CRLF
;

enum HttpMethod {
    HEAD, GET, POST, PUT, PATCH, DELETE, UNKNOWN
};

class Util {
public:
    static void TrimEnd(string &s) {
        while (s.back() == ' ' || s.back() == '\n' || s.back() == '\r') {
            s.pop_back();
        }
    }

    static bool FileExist(string path) {
        struct stat st;
        int res = lstat(path.c_str(), &st);
        return res == 0;
    }

    static bool IsDirectory(string path) {
        struct stat st;
        if (stat(path.c_str(), &st) != 0) {
            return false;
        }
        return S_ISDIR(st.st_mode) == 1;
    }

    static bool IsFileAccessible(string path) {
        if (access(path.c_str(), F_OK) < 0) {
            return false;
        }
        return true;
    }

    static bool IsFileExecutible(string path) {
        if (access(path.c_str(), X_OK) < 0) {
            return false;
        }
        return true;
    }

    static string Exec(string line) {
        const char *cmd = line.c_str();
        char buffer[128];
        string result = "";

        FILE* pipe = popen(cmd, "r");
        if (!pipe) {
            return "";
        }

        try {
            while (!feof(pipe)) {
                if (fgets(buffer, 128, pipe) != NULL) {
                    result += buffer;
                }
            }
        } catch (...) {
            pclose(pipe);
            throw;
        }

        pclose(pipe);
        return result;
    }

    static string UriDecode(const string &sSrc)
    {
        // Source: http://www.codeguru.com/cpp/cpp/algorithms/strings/article.php/c12759/URI-Encoding-and-Decoding.htm
        // Note from RFC1630: "Sequences which start with a percent
        // sign but are not followed by two hexadecimal characters
        // (0-9, A-F) are reserved for future extension"

        const unsigned char * pSrc = (const unsigned char *)sSrc.c_str();
        const int SRC_LEN = sSrc.length();
        const unsigned char * const SRC_END = pSrc + SRC_LEN;
        // last decodable '%'
        const unsigned char * const SRC_LAST_DEC = SRC_END - 2;

        char * const pStart = new char[SRC_LEN];
        char * pEnd = pStart;

        while (pSrc < SRC_LAST_DEC)
        {
          if (*pSrc == '%')
          {
             char dec1, dec2;
             if (-1 != (dec1 = HEX2DEC[*(pSrc + 1)])
                && -1 != (dec2 = HEX2DEC[*(pSrc + 2)]))
             {
                *pEnd++ = (dec1 << 4) + dec2;
                pSrc += 3;
                continue;
             }
          }

          *pEnd++ = *pSrc++;
        }

        // the last 2- chars
        while (pSrc < SRC_END)
          *pEnd++ = *pSrc++;

        string sResult(pStart, pEnd);
        delete [] pStart;
        return sResult;
    }
};

class HttpUtil {
public:
    static string GetAbsolutePath(string path) {
        string wwwroot(getenv("WWWROOT"));
        return wwwroot + path;
    }

    static string GetHttpMethodName(HttpMethod method) {
        switch (method) {
            case HEAD: return "HEAD";
            case GET: return "GET";
            case POST: return "POST";
            case PUT: return "PUT";
            case PATCH: return "PATCH";
            case DELETE: return "DELETE";
            case UNKNOWN: default: return "UNKNOWN";
        }
    }

    static string GetHttpStatusString(int code) {
        switch (code) {
            case 200: return "OK";
            case 301: return "Moved Permanently";
            case 403: return "Forbidden";
            case 404: return "Not Found";
            default: return "Unknown";
        }
    }

    static string GetMIMEType(string ext) {
        if (ext == "html" || ext == "htm") {
            return "text/html";
        } else if (ext == "txt") {
            return "text/plain";
        } else if (ext == "css") {
            return "text/css";
        } else if (ext == "gif") {
            return "image/gif";
        } else if (ext == "jpg") {
            return "image/jpeg";
        } else if (ext == "png") {
            return "image/png";
        } else if (ext == "bmp") {
            return "image/x-ms-bmp";
        } else if (ext == "doc") {
            return "application/msword";
        } else if (ext == "pdf") {
            return "application/pdf";
        } else if (ext == "mp4") {
            return "video/mp4";
        } else if (ext == "swf" || ext == "swfl") {
            return "application/x-shockwave-flash";
        } else if (ext == "ogg") {
            return "audio/ogg";
        } else if (ext == "bz2") {
            return "application/x-bzip2";
        } else if (ext == "gz" || ext == "tar.gz") {
            return "application/x-gzip";
        } else {
            return "";
        }
    }
};

class HttpResponseWriter {
private:
    int fd;
    bool can_write, closed;

    int status_code;
    map<string, string> headers;

public:
    HttpResponseWriter() {
        this->can_write = false;
        this->closed = false;
    }

    HttpResponseWriter(int fd) {
        this->fd = fd;
        this->can_write = false;
        this->closed = false;
    }

    int GetSocketDescriptor() {
        return this->fd;
    }

    int GetStatusCode() {
        return this->status_code;
    }

    void SetStatusCode(int code) {
        this->status_code = code;
    }

    string GetHeader(string name) {
        map<string, string>::iterator it = this->headers.find(name);
        if (it == this->headers.end()) {
            return "";
        }
        return this->headers[name];
    }

    void SetHeader(string name, string value) {
        this->headers[name] = value;
    }

    void RemoveHeader(string name) {
        map<string, string>::iterator it = this->headers.find(name);
        if (it != this->headers.end()) {
            this->headers.erase(it);
        }
    }

    int Write(const char *data, int len) {
        if (closed || !can_write) {
            return -1;
        }
        return write(this->fd, data, len);
    }

    int WriteString(string data) {
        if (closed || !can_write) {
            return -1;
        }
        return write(this->fd, data.c_str(), data.length());
    }

    int WriteFile(string path) {
        int file_fd = open(path.c_str(), O_RDONLY);
        int len = 0;
        char buf[BUF_SIZE];

        while ((len = read(file_fd, buf, BUF_SIZE)) > 0) {
            Write(buf, len);
        }

        close(file_fd);

        return 0;
    }

    int WriteErrorPage() {
        if (this->status_code < 400) {
            return 0;
        }

        switch (this->status_code) {
        case 403:
            return Write(http_error_403_page, strlen(http_error_403_page));
        case 404:
            return Write(http_error_404_page, strlen(http_error_404_page));
        default:
            return Write(http_error_default_page, strlen(http_error_default_page));
        }
    }

    int PrepareForWrite() {
        if (closed) {
            return -1;
        }

        string buf;
        int len;

        buf = "HTTP/1.1 " + to_string(status_code) + " " + HttpUtil::GetHttpStatusString(status_code) + "\r\n";
        if ((len = write(this->fd, buf.c_str(), buf.length())) < 0) {
            exit(1);
        }

        for (map<string, string>::iterator it = this->headers.begin(); it != this->headers.end(); it++) {
            buf = it->first + ": " + it->second + "\r\n";
            if ((len = write(this->fd, buf.c_str(), buf.length())) < 0) {
                exit(1);
            }
        }

        if ((len = write(this->fd, "\r\n", 2)) < 0) {
            exit(1);
        }

        this->can_write = true;
        return 0;
    }

    int SendSpecialResponse(int code) {
        SetStatusCode(code);
        SetHeader("Content-Type", "text/html");
        PrepareForWrite();
        WriteErrorPage();
        Close();

        return 0;
    }

    int SendRedirectResponse(int code, string location) {
        SetStatusCode(code);
        SetHeader("Location", location);
        PrepareForWrite();
        Close();

        return 0;
    }

    void Close() {
        if (closed) {
            return;
        }

        shutdown(fd, 2);
        close(fd);
        this->can_write = false;
        this->closed = true;
    }
};

class HttpRequest {
public:
    HttpMethod method;
    string uri;
    string path;
    string host;
    string query_string;
    map<string, string> headers;
    string content;

    HttpRequest() {

    }

    string GetHeader(string name) {
        map<string, string>::iterator it = this->headers.find(name);
        if (it == this->headers.end()) {
            return "";
        }
        return this->headers[name];
    }

    void Print() {
        cout << "Method: " << HttpUtil::GetHttpMethodName(this->method) << endl;
        cout << "Uri: " << this->uri << endl;
        cout << "  Path: " << this->path << endl;
        cout << "  Query String: " << this->query_string << endl;
        cout << "Host: " << this->host << endl;
        cout << "Headers:" << endl;
        for (map<string, string>::iterator it = this->headers.begin(); it != this->headers.end(); it++) {
            cout << "  " << it->first << ": " << it->second << endl;
        }
        cout << "Content:" << endl;
        if (this->content.length() > 0) {
            cout << this->content << endl;
        } else {
            cout << "(empty)" << endl;
        }
    }
};

class HttpRequestParser {
private:
    static HttpMethod ParseMethod(string token) {
        if (token == "HEAD") {
            return HEAD;
        } else if (token == "GET") {
            return GET;
        } else if (token == "POST") {
            return POST;
        } else if (token == "PUT") {
            return PUT;
        } else if (token == "PATCH") {
            return PATCH;
        } else if (token == "DELETE") {
            return DELETE;
        } else {
            return UNKNOWN;
        }
    }

    static void ParseUri(HttpRequest *request, string token) {
        request->uri = token;

        size_t question_pos = token.find_first_of("?");
        if (question_pos != string::npos) {
            request->path = Util::UriDecode(token.substr(0, question_pos));
            request->query_string = token.substr(question_pos + 1);
        } else {
            request->path = Util::UriDecode(token);
            request->query_string = "";
        }
    }

    static void ParseFirstLine(HttpRequest *request, string line) {
        stringstream ss(line);
        string token;
        int token_no = 0;

        while (getline(ss, token, ' ')) {
            switch (token_no) {
            case 0:
                request->method = ParseMethod(token);
                break;
            case 1:
                ParseUri(request, token);
                break;
            default:
                break;
            }

            token_no++;
        }
    }

    static void ParseHeader(HttpRequest *request, string line) {
        size_t colon_pos = line.find_first_of(":");
        string name = line.substr(0, colon_pos);
        string value = line.substr(colon_pos + 2);

        if (name == "Host") {
            request->host = value;
        } else {
            request->headers[name] = value;
        }
    }

public:
    static HttpRequest Parse(string data) {
        HttpRequest request;

        stringstream ss(data);
        string line;
        int line_no = 0;
        bool in_content = false;

        while (getline(ss, line)) {
            Util::TrimEnd(line);

            if (!in_content && line.length() == 0) {
                request.content = "";
                in_content = true;
                continue;
            }

            if (line_no == 0) {
                ParseFirstLine(&request, line);
            } else if (in_content) {
                request.content += line;
                request.content += "\n";
            } else {
                ParseHeader(&request, line);
            }

            line_no++;
        }

        if (request.content.length() > 0) {
            request.content.pop_back();
        }

        return request;
    }
};

class HttpRequestHandler {
private:
    static bool IsDirectoryRequest(string path) {
        if (path.back() == '/') {
            return true;
        }

        return Util::IsDirectory(HttpUtil::GetAbsolutePath(path));
    }

    static string GetFileExtension(string path) {
        size_t dot_pos = path.find_last_of(".");
        if (dot_pos == string::npos) {
            return "";
        }

        return path.substr(dot_pos + 1);
    }

    static string FindDefaultPage(string path) {
        for (int i = 0; i < 4; i++) {
            if (Util::FileExist(path + DEFAULT_PAGES[i])) {
                return DEFAULT_PAGES[i];
            }
        }

        return "";
    }

    static string GenerateDirectoryOutput(string path) {
        string output(directory_output_template);
        string list = Util::Exec("ls -al " + path);
        list.erase(0, list.find("\n") + 1);
        string list_withlink = "";
        size_t spcae_pos = string::npos;

        stringstream ss(list);
        string line;
        while (getline(ss, line)) {
            Util::TrimEnd(line);

            if (spcae_pos == string::npos) {
                spcae_pos = line.find_last_of(" ");
            }

            size_t arrow_pos = line.find("->");
            string filename = "";
            if (arrow_pos != string::npos) {
                filename = line.substr(spcae_pos + 1, arrow_pos - spcae_pos - 2);
            } else {
                filename = line.substr(spcae_pos + 1);
            }

            line.replace(spcae_pos + 1, filename.length(), "<a href=\"" + filename + "\">" + filename + "</a>");
            list_withlink = list_withlink + line + CRLF;
        }

        output.replace(output.find("{{title}}"), 9, path);
        output.replace(output.find("{{path}}"), 8, path);
        output.replace(output.find("{{list}}"), 8, list_withlink);
        return output;
    }

    static void HandleCgiRequest(HttpRequest *request, HttpResponseWriter *response) {
        string path = HttpUtil::GetAbsolutePath(request->path);

        if (!Util::IsFileExecutible(path)) {
            response->SendSpecialResponse(403);
            return;
        }

        int fd[2];
        if (request->method == POST) {
            pipe(fd);
        }

        int cgi_pid;
        if ((cgi_pid = fork()) < 0) {
            response->SendSpecialResponse(500);
            return;
        } else if (cgi_pid == 0) {
            setenv("QUERY_STRING", request->query_string.c_str(), 1);
            setenv("REQUEST_METHOD", HttpUtil::GetHttpMethodName(request->method).c_str(), 1);
            setenv("SCRIPT_NAME", path.c_str(), 1);

            if (request->method == POST) {
                setenv("CONTENT_LENGTH", to_string(request->content.length()).c_str(), 1);
                setenv("CONTENT_TYPE", request->GetHeader("Content-Type").c_str(), 1);
                close(fd[1]);
                close(0);
                dup2(fd[0], 0);
            }
            dup2(response->GetSocketDescriptor(), 1);

            cout << "HTTP/1.1 200 OK " << CRLF;

            if (execl(path.c_str(), path.c_str(), NULL) < 0) {
                perror("exec error");
                exit(1);
            }

            response->Close();
        } else {
            close(response->GetSocketDescriptor());

            response->SetStatusCode(200);
            if (request->method == POST) {
                close(fd[0]);
                write(fd[1], request->content.c_str(), request->content.length());
                close(fd[1]);
            }
        }
    }

    static void HandleDirectoryRequest(HttpRequest *request, HttpResponseWriter *response) {
        string path = HttpUtil::GetAbsolutePath(request->path);

        if (path.back() != '/') {
            string location = HTTP_PREFIX + request->host + request->path + "/";
            if (request->query_string != "") {
                location = location + "?" + request->query_string;
            }

            response->SendRedirectResponse(301, location);
            return;
        }

        string default_page;
        if ((default_page = FindDefaultPage(path)) != "") {
            request->path += default_page;
            HandleStaticFileRequest(request, response);
            return;
        }

        if (!Util::IsFileExecutible(path)) {
            response->SendSpecialResponse(404);
            return;
        }

        response->SetStatusCode(200);
        response->SetHeader("Content-Type", "text/html");
        response->PrepareForWrite();
        response->WriteString(GenerateDirectoryOutput(path));
        response->Close();
    }

    static void HandleStaticFileRequest(HttpRequest *request, HttpResponseWriter *response) {
        string path = HttpUtil::GetAbsolutePath(request->path);

        if (!Util::FileExist(path)) {
            response->SendSpecialResponse(404);
            return;
        }

        if (!Util::IsFileAccessible(path)) {
            response->SendSpecialResponse(403);
            return;
        }

        string ext = GetFileExtension(path);
        if (ext == "cgi" || ext == "CGI") {
            HandleCgiRequest(request, response);
            return;
        }

        string mime;
        if ((mime = HttpUtil::GetMIMEType(ext)) != "") {
            response->SetHeader("Content-Type", mime);
        }

        response->SetStatusCode(200);
        response->PrepareForWrite();
        response->WriteFile(path);
        response->Close();
    }

    static void PrintLog(HttpRequest *request, HttpResponseWriter *response) {
        cout << HttpUtil::GetHttpMethodName(request->method) << " " << request->uri << " ";
        cout << response->GetStatusCode() << " " << HttpUtil::GetHttpStatusString(response->GetStatusCode()) << endl;
    }

public:
    static void HandleRequest(HttpRequest *request, HttpResponseWriter *response) {
        if (IsDirectoryRequest(request->path)) {
            HandleDirectoryRequest(request, response);
        } else {
            HandleStaticFileRequest(request, response);
        }

        PrintLog(request, response);
    }
};

class HttpServer {
private:
    char *port;
    char *wwwroot;
    int max_clients;
    int msock;
    bool ready, stop;

    int BindAndListen() {
        this->msock = socket(PF_INET, SOCK_STREAM, 0);
        if (this->msock < 0) {
            perror("cannot create socket");
            return -1;
        }

        int enable = 1;
        if (setsockopt(this->msock, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(int)) < 0) {
            perror("setsockopt error");
            close(this->msock);
            return -1;
        }

        struct sockaddr_in sin;
        bzero((char*)&sin, sizeof(sin));
        sin.sin_family = AF_INET;
        sin.sin_addr.s_addr = INADDR_ANY;
        sin.sin_port = htons((u_short)atoi(this->port));

        if (::bind(this->msock, (struct sockaddr *)&sin, sizeof(sin)) < 0) {
            perror("bind error");
            close(this->msock);
            return -1;
        }

        if (listen(this->msock, DEFAULT_MAX_CLIENTS) < 0) {
            perror("listen error");
            close(this->msock);
            return -1;
        }

        return 0;
    }

    void SetSignalHandler() {
        struct sigaction sa;
        sa.sa_handler = SIG_IGN;
        sa.sa_flags = SA_NOCLDWAIT;
        if (sigaction(SIGCHLD, &sa, NULL) == -1) {
            perror("sigaction");
            exit(1);
        }
    }

    int ServerLoop() {
        struct sockaddr_in client_addr;
        socklen_t clilen = sizeof(client_addr);
        int ssock, child_pid;

        while (true) {
            ssock = accept(this->msock, (struct sockaddr *)&client_addr, &clilen);

            if (this->stop) {
                close(ssock);
                break;
            }

            if (ssock < 0) {
                perror("accept error");
                break;
            }

            if ((child_pid = fork()) < 0) {
                perror("fork error");
                break;
            } else if (child_pid == 0) {
                close(this->msock);
                HttpRequest request = ParseRequest(ssock);
                HttpResponseWriter response(ssock);
                HttpRequestHandler::HandleRequest(&request, &response);
                exit(0);
            } else {
                close(ssock);
            }
        }

        return 0;
    }

    HttpRequest ParseRequest(int ssock) {
        char buf[BUF_SIZE];
        int len;

        if ((len = read(ssock, buf, sizeof(buf))) < 0) {
            exit(1);
        }

        string buf_str(buf);
        HttpRequest request = HttpRequestParser::Parse(buf_str);

        return request;
    }

public:
    HttpServer() {

    }

    HttpServer(char *port, char *wwwroot) {
        this->port = strdup(port);
        this->wwwroot = strdup(wwwroot);
        this->ready = false;
        this->max_clients = DEFAULT_MAX_CLIENTS;
    }

    int IsReady() {
        return this->ready;
    }

    int Start() {
        if (IsReady()) {
            return 0;
        }

        if (BindAndListen() < 0) {
            return -1;
        }

        setenv("WWWROOT", this->wwwroot, 1);
        this->ready = true;
        this->stop = false;
        SetSignalHandler();
        ServerLoop();

        return 0;
    }

    int Stop() {
        if (!IsReady()) {
            return 0;
        }

        this->stop = true;
        close(this->msock);

        return 0;
    }
};

int main(int argc, char* argv[])
{
    if (argc != 3) {
        printf("usage: %s <port_no> <wwwroot_path>\n", argv[0]);
        exit(1);
    }

    char *port = strdup(argv[1]);
    char *wwwroot = strdup(argv[2]);

    printf("port: %s\n", port);
    printf("wwwroot: %s\n", wwwroot);

    HttpServer server(port, wwwroot);
    server.Start();

    return 0;
}
