//===============================Standard C libraries & Definitions=====================
#include <stdio.h>      // Standard library
#include <stdlib.h>     // Memory manegement
#include <string.h>     // Text alteration
#include <stdbool.h>    // True or false
#include <time.h>       // Get current time
//======================================================================================
#include <winsock2.h>  // Windows API Tool's(application program interface Tools)
#include <WS2tcpip.h>  // Windows socket library's
#include <Windows.h>   // library for sleep() and other win_func..
#pragma comment(lib, "ws2_32.lib")  // import library on mingw systems visual studio

// CONFIG these or pass as argv: server.exe <port> <root> // only Theo
// CONSTANTS
#define DEFAULT_PORT     "8080"         // Port number standard  "P_O_R_T"
#define DEFAULT_ROOT     "./www"        // directory for html/css/js files "R_O_O_T"
#define MAX_CONNECTIONS  20             // listen() backlog
#define RECV_TIMEOUT_SEC 5              // seconds before it drops a stalled client
#define BUFFER_SIZE      8192           // bytes at a time read from a client
#define LOG_FILE         "server.log"   // log info of client

// GLOBALS VARIABLES--written once at startup, then read-only
static char g_root[MAX_PATH];   // document root 
static char g_port[32];         // port string
static SOCKET g_server_sock = INVALID_SOCKET;
static FILE  *g_log_fp = NULL;
//=======================================================================================

//=======================================================================================
// Logging
// Writes a timestamped line to both stdout and server.log

void log_msg(const char *level, const char *fmt, ...) {
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);

    time_t now = time(NULL);
    struct tm *t = localtime(&now);
    char ts[32];
    strftime(ts, sizeof(ts), "%Y-%m-%d %H:%M:%S", t);

    printf("[%s] %s  %s\n", ts, level, buf);
    if (g_log_fp) {
        fprintf(g_log_fp, "[%s] %s  %s\n", ts, level, buf);
        fflush(g_log_fp);
    }
}

#define LOG_INFO(...)  log_msg("INFO ", __VA_ARGS__)
#define LOG_WARN(...)  log_msg("WARN ", __VA_ARGS__)
#define LOG_ERROR(...) log_msg("ERROR", __VA_ARGS__)

// SHUTDOWN
// Ctrl+C so the port is released cleanly

BOOL WINAPI ctrl_handler(DWORD type) {
    if (type == CTRL_C_EVENT || type == CTRL_CLOSE_EVENT) {
        LOG_INFO("Shutting down...");
        if (g_server_sock != INVALID_SOCKET) {
            closesocket(g_server_sock);
            g_server_sock = INVALID_SOCKET;
        }
        WSACleanup();
        if (g_log_fp) fclose(g_log_fp);
        exit(0);
    }
    return FALSE;
}

// MIME TYPE TABLE
// Maps file extensions to Content-Type values so browsers
// know what they're receiving.

typedef struct { const char *ext; const char *mime; } MimeEntry;

static const MimeEntry mime_table[] = {
    { "html", "text/html; charset=utf-8"  },
    { "htm",  "text/html; charset=utf-8"  },
    { "css",  "text/css"                  },
    { "js",   "application/javascript"    },
    { "json", "application/json"          },
    { "png",  "image/png"                 },
    { "jpg",  "image/jpeg"                },
    { "jpeg", "image/jpeg"                },
    { "gif",  "image/gif"                 },
    { "svg",  "image/svg+xml"             },
    { "ico",  "image/x-icon"              },
    { "txt",  "text/plain"                },
    { "pdf",  "application/pdf"           },
    { NULL,   "application/octet-stream"  },  // default / fallback
};

const char *get_mime(const char *path) {
    const char *dot = strrchr(path, '.');
    if (dot) {
        dot++; // skip the '.'
        for (int i = 0; mime_table[i].ext != NULL; i++) {
            if (_stricmp(dot, mime_table[i].ext) == 0)
                return mime_table[i].mime;
        }
    }
    return "application/octet-stream";
}

// HTTP REQUEST PARSER
// Pulls out: method, path, HTTP version from the first line.
// Stops at the blank line that separates headers from body.

typedef struct {
    char method[16];
    char path[1024];
    char version[16];
} HttpRequest;

bool parse_request(const char *raw, HttpRequest *req) {
    // Read the request line: "GET /index.html HTTP/1.1"
    if (sscanf(raw, "%15s %1023s %15s", req->method, req->path, req->version) != 3)
        return false;

    // Basic path sanitization: reject anything with ".." to prevent
    // directory traversal attacks (e.g. "GET /../../../etc/passwd")

    if (strstr(req->path, ".."))
        return false;

    return true;
}

// SEND HELPERS

// Send a plain status response with a short HTML body

void send_status(SOCKET client, int code, const char *reason) {
    char buf[512];
    int len = snprintf(buf, sizeof(buf),
        "HTTP/1.1 %d %s\r\n"
        "Content-Type: text/html\r\n"
        "Connection: close\r\n"
        "\r\n"
        "<html><body><h1>%d %s</h1></body></html>\r\n",
        code, reason, code, reason);
    send(client, buf, len, 0);
}

// Send a file from disk back to the client

void send_file(SOCKET client, const char *filepath) {
    FILE *fp = fopen(filepath, "rb");
    if (!fp) {
        send_status(client, 404, "Not Found");
        return;
    }

    // Get file size for Content-Length header

    fseek(fp, 0, SEEK_END);
    long file_size = ftell(fp);
    rewind(fp);

    const char *mime = get_mime(filepath);

    // Send headers first

    char headers[512];
    int hlen = snprintf(headers, sizeof(headers),
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: %s\r\n"
        "Content-Length: %ld\r\n"
        "Connection: close\r\n"
        "\r\n",
        mime, file_size);
    send(client, headers, hlen, 0);

    // Send file body in chunks

    char chunk[4096];
    size_t n;
    while ((n = fread(chunk, 1, sizeof(chunk), fp)) > 0) {
        send(client, chunk, (int)n, 0);
    }

    fclose(fp);
}

// PER-CONNECTION HANDLER
// This function runs in its own thread for each client so
// one slow request never blocks others.

typedef struct {
    SOCKET  fd;
    char    ip[INET_ADDRSTRLEN];
} ClientCtx;

DWORD WINAPI handle_client(LPVOID arg) {
    ClientCtx *ctx = (ClientCtx *)arg;
    SOCKET client  = ctx->fd;
    char   ip[INET_ADDRSTRLEN];
    strncpy(ip, ctx->ip, sizeof(ip));
    free(ctx);

    // --- Set receive timeout so stalled clients don't hang the thread ---

    DWORD timeout_ms = RECV_TIMEOUT_SEC * 1000;
    setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, (const char *)&timeout_ms, sizeof(timeout_ms));

    // --- Read the HTTP request (keep reading until we see \r\n\r\n) ---

    char buffer[BUFFER_SIZE] = {0};
    int  total = 0;
    int  bytes;

    while (total < BUFFER_SIZE - 1) {
        bytes = recv(client, buffer + total, BUFFER_SIZE - 1 - total, 0);
        if (bytes <= 0) break;
        total += bytes;
        if (strstr(buffer, "\r\n\r\n")) break;  // end of HTTP headers
    }

    if (total == 0) {
        closesocket(client);
        return 0;
    }

    // --- Parse the request line ---

    HttpRequest req = {0};
    if (!parse_request(buffer, &req)) {
        send_status(client, 400, "Bad Request");
        closesocket(client);
        return 0;
    }

    LOG_INFO("%s  %s %s", ip, req.method, req.path);

    // Only handle GET for now
    if (_stricmp(req.method, "GET") != 0) {
        send_status(client, 405, "Method Not Allowed");
        closesocket(client);
        return 0;
    }

    // Resolve path to a real file
    // "/"  -> serve index.html automatically
    
    char filepath[MAX_PATH];
    if (strcmp(req.path, "/") == 0) {
        snprintf(filepath, sizeof(filepath), "%s/index.html", g_root);
    } else {
        snprintf(filepath, sizeof(filepath), "%s%s", g_root, req.path);
    }

    // Normalize backslashes for Windows
    
    for (char *p = filepath; *p; p++) {
        if (*p == '/') *p = '\\';
    }

    // Check the file exists before trying to send it
    
    DWORD attr = GetFileAttributesA(filepath);
    if (attr == INVALID_FILE_ATTRIBUTES || (attr & FILE_ATTRIBUTE_DIRECTORY)) {
        LOG_WARN("404  %s", filepath);
        send_status(client, 404, "Not Found");
    } else {
        send_file(client, filepath);
    }

    closesocket(client);
    return 0;
}

// MAIN
// Usage: server.exe [port] [root_directory]
//   e.g. server.exe 9000 C:\MyWebsite

int main(int argc, char *argv[]) {

    // --- Parse optional command-line arguments ---

    strncpy(g_port, (argc > 1) ? argv[1] : DEFAULT_PORT, sizeof(g_port));
    strncpy(g_root, (argc > 2) ? argv[2] : DEFAULT_ROOT, sizeof(g_root));

    // Strip trailing slash from root
    size_t rlen = strlen(g_root);
    if (rlen > 0 && (g_root[rlen-1] == '/' || g_root[rlen-1] == '\\'))
        g_root[rlen-1] = '\0';

    // --- Open log file ---
    g_log_fp = fopen(LOG_FILE, "a");
    if (!g_log_fp) LOG_WARN("Could not open log file %s", LOG_FILE);

    // --- Register Ctrl+C handler ---
    SetConsoleCtrlHandler(ctrl_handler, TRUE);

    // --- Winsock init ---
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        LOG_ERROR("WSAStartup failed: %d", WSAGetLastError());
        return 1;
    }

    // --- Create listening socket ---
    g_server_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (g_server_sock == INVALID_SOCKET) {
        LOG_ERROR("socket() failed: %d", WSAGetLastError());
        WSACleanup();
        return 1;
    }

    // Allow quick restart without "address already in use" errors
    int opt = 1;
    setsockopt(g_server_sock, SOL_SOCKET, SO_REUSEADDR, (const char *)&opt, sizeof(opt));

    // --- Resolve and bind ---
    struct addrinfo hints = {0}, *res;
    hints.ai_family   = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags    = AI_PASSIVE;   // bind to 0.0.0.0 (all interfaces)

    if (getaddrinfo(NULL, g_port, &hints, &res) != 0) {
        LOG_ERROR("getaddrinfo failed: %d", WSAGetLastError());
        closesocket(g_server_sock);
        WSACleanup();
        return 1;
    }

    if (bind(g_server_sock, res->ai_addr, (int)res->ai_addrlen) == SOCKET_ERROR) {
        LOG_ERROR("bind() failed: %d", WSAGetLastError());
        freeaddrinfo(res);
        closesocket(g_server_sock);
        WSACleanup();
        return 1;
    }
    freeaddrinfo(res);

    // --- Start listening ---
    if (listen(g_server_sock, MAX_CONNECTIONS) == SOCKET_ERROR) {
        LOG_ERROR("listen() failed: %d", WSAGetLastError());
        closesocket(g_server_sock);
        WSACleanup();
        return 1;
    }

    system("cls");
    LOG_INFO("Server started on port %s", g_port);
    LOG_INFO("Serving files from: %s", g_root);
    LOG_INFO("Press Ctrl+C to stop");

    // --- Accept loop ---
    while (1) {
        struct sockaddr_in client_addr;
        int addr_len = sizeof(client_addr);

        SOCKET client_fd = accept(g_server_sock, (struct sockaddr *)&client_addr, &addr_len);
        if (client_fd == INVALID_SOCKET) {
            // Server socket was closed (Ctrl+C), exit cleanly
            if (g_server_sock == INVALID_SOCKET) break;
            LOG_WARN("accept() failed: %d", WSAGetLastError());
            continue;
        }

        // Pack connection info for the thread
        ClientCtx *ctx = (ClientCtx *)malloc(sizeof(ClientCtx));
        ctx->fd = client_fd;
        inet_ntop(AF_INET, &client_addr.sin_addr, ctx->ip, sizeof(ctx->ip));

        // Spawn a thread — each client is handled independently
        HANDLE thread = CreateThread(NULL, 0, handle_client, ctx, 0, NULL);
        if (thread) {
            CloseHandle(thread);  // detach; thread cleans itself up
        } else {
            LOG_ERROR("CreateThread failed");
            closesocket(client_fd);
            free(ctx);
        }
    }

    // --- Cleanup ---
    closesocket(g_server_sock);
    WSACleanup();
    if (g_log_fp) fclose(g_log_fp);
    return 0;
}