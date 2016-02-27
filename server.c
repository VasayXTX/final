#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <libev/ev.h>

#define CLIENT_BUF_IN_SIZE 4096
#define CLIENT_BUF_OUT_SIZE 4096

struct ev_io_http {
    struct ev_io io;
    char *root_dir;
};

extern int opterr;

int parse_cli_args(int argc, char *const argv[], char **ip, int *port, char **dir) {
    int opt;
    opterr = 0;
    *ip = NULL;
    *dir = NULL;
    *port = 0;
    while ((opt = getopt(argc, argv, "h:p:d:")) != -1) {
        switch (opt) {
        case 'h':
            *ip = (char *)malloc(strlen(optarg) + 1);
            strcpy(*ip, optarg);
            break;

        case 'p':
            *port = atoi(optarg);
            break;
        case 'd':
            *dir = (char *)malloc(strlen(optarg) + 1);
            strcpy(*dir, optarg);
            break;
        }
    }
    if (!*ip || !*port || !*dir) {
        if (*ip) free(*ip);
        if (*dir) free(*dir);
        return -1;
    }
    return 0;
}

int start_socket(char *ip, int port) {
    int sd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    int optval = 1;
    setsockopt(sd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval));

    struct sockaddr_in addr;
    bzero(&addr, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    if (inet_aton(ip, &addr.sin_addr) == 0)  {
        perror("inet_aton");
        exit(EXIT_FAILURE);
    }
    if (bind(sd, (struct sockaddr *)&addr, sizeof(addr)) == -1) {
        perror("bind");
        exit(EXIT_FAILURE);
    }
    if (listen(sd, SOMAXCONN) == -1) {
        perror("listen");
        exit(EXIT_FAILURE);
    }

    return sd;
}

char *get_path_from_http_request(char *http_request) {
    size_t l = strlen(http_request);
    size_t b = 0, e = 0;
    while (b < l && http_request[b] != '/') b++;
    if (b == l) return NULL;
    e = b + 1;
    while (e < l && http_request[e] != ' ' && http_request[e] != '?') e++;
    if (e == l) return NULL;
    char *path = (char *)malloc(e - b + 1);
    memcpy(path, http_request + b, e - b);
    path[e-b] = '\0';
    return path;
}

void process_http_request(char *input, char *root_dir, char *output) {
    memset(output, '\0', CLIENT_BUF_OUT_SIZE);

    int status_code = 0;
    char *status_description;
    char *body = NULL;
    char headers[4096];
    memset(headers, '\0', 4096);
    strcat(headers, "Content-Type: text/html\r\n");

    char *path = get_path_from_http_request(input);
    if (path != NULL) {
        char *full_path = (char *)malloc(strlen(root_dir) + strlen(path) + sizeof("\0"));
        strcpy(full_path, root_dir);
        strcat(full_path, path);
        if (access(full_path, F_OK) == 0) {
            FILE *f = fopen(full_path, "r");
            if (f == NULL) {
                status_code = 403;
                status_description = "FORBIDDEN";
            } else {
                fseek(f, 0, SEEK_END);
                long f_size = ftell(f);
                fseek(f, 0, SEEK_SET);
                body = (char *)malloc(f_size + 1);
                fread(body, 1, f_size, f);
                body[f_size] = '\0';
                fclose(f);

                char cl_header[1024];
                sprintf(cl_header, "Content-Length: %d\r\n", f_size);
                strcat(headers, cl_header);

                status_code = 200;
                status_description = "OK";
            }
        } else {
            status_code = 404;
            status_description = "NOT FOUND";
        }
    } else {
        status_code = 400;
        status_description = "BAD REQUEST";
    }
    sprintf(
        output,
        "HTTP/1.0 %d %s\r\n%s\r\n%s",
        status_code,
        status_description,
        headers == NULL ? "" : headers,
        body == NULL ? "" : body
    );
}

void read_from_client_cb(struct ev_loop *loop, struct ev_io *watcher, int revents) {
    struct ev_io_http *w = (struct ev_io_http *)watcher;

    char buf_in[CLIENT_BUF_IN_SIZE];
    // TODO: use buffered recv
    int read_len = recv(w->io.fd, &buf_in, CLIENT_BUF_IN_SIZE, MSG_NOSIGNAL);
    printf("recv: %d bytes\n", read_len);
    if (read_len == -1) {
        perror("recv");
        exit(EXIT_FAILURE);
    }
    if (read_len == 0) {
        ev_io_stop(loop, &w->io);
        free(w);
        return;
    }

    char buf_out[CLIENT_BUF_OUT_SIZE];
    process_http_request(buf_in, w->root_dir, buf_out);

    // TODO: use buffered send
    send(watcher->fd, buf_out, strlen(buf_out) + 1, MSG_NOSIGNAL);

    shutdown(w->io.fd, SHUT_RDWR);
    close(w->io.fd);
    ev_io_stop(loop, &w->io);
}

void accept_client_cb(struct ev_loop *loop, struct ev_io *watcher, int revents) {
    struct ev_io_http *w = (struct ev_io_http *)watcher;

    int client_sd = accept(w->io.fd, 0, 0);
    struct ev_io_http *watcher_client = (struct ev_io_http *)malloc(sizeof(struct ev_io_http));
    watcher_client->root_dir = w->root_dir;
    ev_io_init(&watcher_client->io, read_from_client_cb, client_sd, EV_READ);
    ev_io_start(loop, &watcher_client->io);
}

int main(int argc, char *const argv[]) {
    char *ip;
    int port = 0;
    char *dir;

    if (parse_cli_args(argc, argv, &ip, &port, &dir) != 0) {
        printf("Invalid args\n");
        exit(EXIT_FAILURE);
    }

    printf("host: %s\n", ip);
    printf("port: %d\n", port);
    printf("dir: %s\n", dir);

    int sd = start_socket(ip, port);

    struct ev_loop *loop = ev_default_loop(0);
    struct ev_io_http watcher_accept;
    watcher_accept.root_dir = dir;
    ev_io_init(&watcher_accept.io, accept_client_cb, sd, EV_READ);
    ev_io_start(loop, &watcher_accept.io);

    while (1) {
        ev_loop(loop, 0);
    }

    free(ip);
    free(dir);

    return 0;
}
