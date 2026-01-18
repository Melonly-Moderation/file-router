#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <errno.h>
#include <signal.h>

#define PORT 8080
#define MAX_ID_LENGTH 100
#define BUFFER_SIZE 65536
#define IMAGE_BUFFER_SIZE (10 * 1024 * 1024)
#define CDN_HOST "cdn_zipline"
#define CDN_PORT 3000

static char image_buffer[IMAGE_BUFFER_SIZE];

static int is_valid_id(const char *id, size_t len) {
    if (len == 0 || len > MAX_ID_LENGTH) return 0;
    for (size_t i = 0; i < len; i++) {
        char c = id[i];
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || 
              (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '/' || c == '.')) {
            return 0;
        }
    }
    if (strstr(id, "..") != NULL) return 0;
    return 1;
}

static int ends_with(const char *str, const char *suffix) {
    size_t str_len = strlen(str);
    size_t suffix_len = strlen(suffix);
    if (suffix_len > str_len) return 0;
    return strcmp(str + str_len - suffix_len, suffix) == 0;
}

static int connect_to_cdn(void) {
    struct hostent *he = gethostbyname(CDN_HOST);
    if (!he) return -1;
    
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) return -1;
    
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(CDN_PORT);
    memcpy(&addr.sin_addr, he->h_addr_list[0], he->h_length);
    
    struct timeval tv;
    tv.tv_sec = 10;
    tv.tv_usec = 0;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
    
    if (connect(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        close(sock);
        return -1;
    }
    return sock;
}

static int fetch_image(const char *path, size_t *out_size, char *content_type, size_t ct_size) {
    int sock = connect_to_cdn();
    if (sock < 0) return -1;
    
    char request[512];
    int req_len = snprintf(request, sizeof(request),
        "GET /u/%s HTTP/1.1\r\nHost: %s:%d\r\nConnection: close\r\n\r\n",
        path, CDN_HOST, CDN_PORT);
    
    if (send(sock, request, req_len, 0) != req_len) {
        close(sock);
        return -1;
    }
    
    char header_buf[4096];
    size_t header_len = 0;
    char *body_start = NULL;
    
    while (header_len < sizeof(header_buf) - 1) {
        ssize_t n = recv(sock, header_buf + header_len, 1, 0);
        if (n <= 0) {
            close(sock);
            return -1;
        }
        header_len++;
        header_buf[header_len] = '\0';
        if (header_len >= 4 && strcmp(header_buf + header_len - 4, "\r\n\r\n") == 0) {
            body_start = header_buf + header_len;
            break;
        }
    }
    
    if (!body_start) {
        close(sock);
        return -1;
    }
    
    if (strstr(header_buf, "HTTP/1.1 200") == NULL && strstr(header_buf, "HTTP/1.0 200") == NULL) {
        close(sock);
        return -1;
    }
    
    content_type[0] = '\0';
    char *ct = strcasestr(header_buf, "Content-Type:");
    if (ct) {
        ct += 13;
        while (*ct == ' ') ct++;
        char *end = strpbrk(ct, "\r\n");
        if (end) {
            size_t len = end - ct;
            if (len >= ct_size) len = ct_size - 1;
            memcpy(content_type, ct, len);
            content_type[len] = '\0';
        }
    }
    
    if (strncmp(content_type, "image/", 6) != 0) {
        close(sock);
        return -1;
    }
    
    size_t total = 0;
    while (total < IMAGE_BUFFER_SIZE) {
        ssize_t n = recv(sock, image_buffer + total, IMAGE_BUFFER_SIZE - total, 0);
        if (n <= 0) break;
        total += n;
    }
    
    close(sock);
    *out_size = total;
    return (total > 0) ? 0 : -1;
}

static int try_fetch_with_extensions(const char *id, size_t *out_size, char *content_type, size_t ct_size) {
    if (strchr(id, '/') != NULL || ends_with(id, ".webp") || ends_with(id, ".png") || 
        ends_with(id, ".jpg") || ends_with(id, ".jpeg")) {
        return fetch_image(id, out_size, content_type, ct_size);
    }
    
    char path[256];
    
    snprintf(path, sizeof(path), "%s.webp", id);
    if (fetch_image(path, out_size, content_type, ct_size) == 0) return 0;
    
    snprintf(path, sizeof(path), "%s.png", id);
    if (fetch_image(path, out_size, content_type, ct_size) == 0) return 0;
    
    snprintf(path, sizeof(path), "%s.jpg", id);
    if (fetch_image(path, out_size, content_type, ct_size) == 0) return 0;
    
    return -1;
}

static void send_404(int client) {
    const char *response = "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
    send(client, response, strlen(response), 0);
}

static void handle_client(int client) {
    char request[2048];
    ssize_t n = recv(client, request, sizeof(request) - 1, 0);
    if (n <= 0) {
        send_404(client);
        return;
    }
    request[n] = '\0';
    
    if (strncmp(request, "GET /", 5) != 0) {
        send_404(client);
        return;
    }
    
    char *path_start = request + 5;
    char *path_end = strchr(path_start, ' ');
    if (!path_end) {
        send_404(client);
        return;
    }
    
    size_t path_len = path_end - path_start;
    if (path_len == 0 || path_len > MAX_ID_LENGTH) {
        send_404(client);
        return;
    }
    
    char id[MAX_ID_LENGTH + 1];
    memcpy(id, path_start, path_len);
    id[path_len] = '\0';
    
    if (!is_valid_id(id, path_len)) {
        send_404(client);
        return;
    }
    
    size_t image_size;
    char content_type[128];
    
    if (try_fetch_with_extensions(id, &image_size, content_type, sizeof(content_type)) != 0) {
        send_404(client);
        return;
    }
    
    char header[512];
    int header_len = snprintf(header, sizeof(header),
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: %s\r\n"
        "Content-Length: %zu\r\n"
        "Cache-Control: public, max-age=3600\r\n"
        "Access-Control-Allow-Origin: *\r\n"
        "Connection: close\r\n\r\n",
        content_type, image_size);
    
    send(client, header, header_len, 0);
    
    size_t sent = 0;
    while (sent < image_size) {
        ssize_t s = send(client, image_buffer + sent, image_size - sent, 0);
        if (s <= 0) break;
        sent += s;
    }
}

int main(void) {
    signal(SIGPIPE, SIG_IGN);
    
    int server = socket(AF_INET, SOCK_STREAM, 0);
    if (server < 0) {
        perror("socket");
        return 1;
    }
    
    int opt = 1;
    setsockopt(server, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(PORT);
    
    if (bind(server, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind");
        return 1;
    }
    
    if (listen(server, 128) < 0) {
        perror("listen");
        return 1;
    }
    
    printf("Server running on port %d\n", PORT);
    
    while (1) {
        int client = accept(server, NULL, NULL);
        if (client < 0) continue;
        handle_client(client);
        close(client);
    }
    
    return 0;
}
