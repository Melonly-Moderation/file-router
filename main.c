#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <signal.h>

#define PORT 8080
#define BUFFER_SIZE 1048576
#define MAX_ID_LENGTH 100
#define CDN_HOST "cdn_zipline"
#define CDN_PORT 3000

static char request_buffer[4096];
static char response_buffer[BUFFER_SIZE];
static char image_buffer[BUFFER_SIZE];
static char http_request[512];
static char id_buffer[MAX_ID_LENGTH + 1];

static const char *extensions[] = {".webp", ".png", ".jpg"};
static const char *content_types[] = {"image/webp", "image/png", "image/jpeg"};

int is_valid_id(const char *id) {
    int len = strlen(id);
    if (len == 0 || len > MAX_ID_LENGTH) return 0;
    for (int i = 0; i < len; i++) {
        char c = id[i];
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || 
              (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '/' || c == '.')) {
            return 0;
        }
    }
    if (strstr(id, "..")) return 0;
    return 1;
}

int has_extension(const char *id) {
    return strstr(id, "/") != NULL || 
           (strlen(id) > 5 && (strcmp(id + strlen(id) - 5, ".webp") == 0)) ||
           (strlen(id) > 4 && (strcmp(id + strlen(id) - 4, ".png") == 0 ||
                               strcmp(id + strlen(id) - 4, ".jpg") == 0 ||
                               strcmp(id + strlen(id) - 5, ".jpeg") == 0));
}

int fetch_image(const char *path, const char **out_content_type) {
    struct hostent *server = gethostbyname(CDN_HOST);
    if (!server) return -1;
    
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) return -1;
    
    struct timeval timeout;
    timeout.tv_sec = 10;
    timeout.tv_usec = 0;
    setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    setsockopt(sockfd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
    
    struct sockaddr_in serv_addr;
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    memcpy(&serv_addr.sin_addr.s_addr, server->h_addr, server->h_length);
    serv_addr.sin_port = htons(CDN_PORT);
    
    if (connect(sockfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        close(sockfd);
        return -1;
    }
    
    snprintf(http_request, sizeof(http_request),
             "GET /u/%s HTTP/1.1\r\nHost: %s:%d\r\nConnection: close\r\n\r\n",
             path, CDN_HOST, CDN_PORT);
    
    if (write(sockfd, http_request, strlen(http_request)) < 0) {
        close(sockfd);
        return -1;
    }
    
    int total = 0;
    int n;
    while ((n = read(sockfd, image_buffer + total, BUFFER_SIZE - total - 1)) > 0) {
        total += n;
        if (total >= BUFFER_SIZE - 1) break;
    }
    close(sockfd);
    image_buffer[total] = '\0';
    
    if (total == 0) return -1;
    
    char *status_line = image_buffer;
    if (strstr(status_line, "200") == NULL) return -1;
    
    char *ct_start = strstr(image_buffer, "Content-Type:");
    if (!ct_start) ct_start = strstr(image_buffer, "content-type:");
    if (ct_start) {
        ct_start += 13;
        while (*ct_start == ' ') ct_start++;
        if (strncmp(ct_start, "image/webp", 10) == 0) *out_content_type = content_types[0];
        else if (strncmp(ct_start, "image/png", 9) == 0) *out_content_type = content_types[1];
        else if (strncmp(ct_start, "image/jpeg", 10) == 0) *out_content_type = content_types[2];
        else return -1;
    } else {
        return -1;
    }
    
    char *body = strstr(image_buffer, "\r\n\r\n");
    if (!body) return -1;
    body += 4;
    
    int body_len = total - (body - image_buffer);
    memmove(image_buffer, body, body_len);
    return body_len;
}

int try_fetch_image(const char *id, const char **out_content_type) {
    if (has_extension(id)) {
        return fetch_image(id, out_content_type);
    }
    
    char path[MAX_ID_LENGTH + 10];
    for (int i = 0; i < 3; i++) {
        snprintf(path, sizeof(path), "%s%s", id, extensions[i]);
        int len = fetch_image(path, out_content_type);
        if (len > 0) return len;
    }
    return -1;
}

void send_404(int client_fd) {
    const char *response = "HTTP/1.1 404 Not Found\r\n"
                          "Access-Control-Allow-Origin: *\r\n"
                          "Content-Length: 0\r\n"
                          "Connection: close\r\n\r\n";
    write(client_fd, response, strlen(response));
}

void send_image(int client_fd, const char *content_type, int len) {
    snprintf(response_buffer, sizeof(response_buffer),
             "HTTP/1.1 200 OK\r\n"
             "Access-Control-Allow-Origin: *\r\n"
             "Content-Type: %s\r\n"
             "Cache-Control: public, max-age=3600\r\n"
             "Content-Length: %d\r\n"
             "Connection: close\r\n\r\n",
             content_type, len);
    write(client_fd, response_buffer, strlen(response_buffer));
    write(client_fd, image_buffer, len);
}

void handle_client(int client_fd) {
    int n = read(client_fd, request_buffer, sizeof(request_buffer) - 1);
    if (n <= 0) {
        send_404(client_fd);
        return;
    }
    request_buffer[n] = '\0';
    
    if (strncmp(request_buffer, "GET /", 5) != 0) {
        send_404(client_fd);
        return;
    }
    
    char *path_start = request_buffer + 5;
    char *path_end = strchr(path_start, ' ');
    if (!path_end) {
        send_404(client_fd);
        return;
    }
    
    int id_len = path_end - path_start;
    if (id_len <= 0 || id_len > MAX_ID_LENGTH) {
        send_404(client_fd);
        return;
    }
    
    memcpy(id_buffer, path_start, id_len);
    id_buffer[id_len] = '\0';
    
    if (!is_valid_id(id_buffer)) {
        send_404(client_fd);
        return;
    }
    
    printf("Request for image ID: %s\n", id_buffer);
    
    const char *content_type = NULL;
    int image_len = try_fetch_image(id_buffer, &content_type);
    
    if (image_len <= 0 || !content_type) {
        printf("Image not found for ID: %s\n", id_buffer);
        send_404(client_fd);
        return;
    }
    
    send_image(client_fd, content_type, image_len);
    printf("Served %s for ID: %s\n", content_type, id_buffer);
}

int main() {
    signal(SIGPIPE, SIG_IGN);
    
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        perror("socket");
        return 1;
    }
    
    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(PORT);
    
    if (bind(server_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind");
        return 1;
    }
    
    if (listen(server_fd, 10) < 0) {
        perror("listen");
        return 1;
    }
    
    printf("Server running on port %d\n", PORT);
    
    while (1) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        int client_fd = accept(server_fd, (struct sockaddr *)&client_addr, &client_len);
        if (client_fd < 0) continue;
        handle_client(client_fd);
        close(client_fd);
    }
    
    return 0;
}
