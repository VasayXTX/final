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

char *trim(const char *str) {
    size_t l = 0;
    size_t r = strlen(str);
    while (l < r && isspace(str[l])) l++;
    while (r > 0 && isspace(str[r-1])) r--;
    if (l > r) return "";
    char *res = (char *)malloc((r - l + 1) * sizeof(char));
    res[r-l] = '\0';
    memcpy(res, str + l, r - l);
    return res;
}

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

void read_from_client_cb(struct ev_loop *loop, struct ev_io *watcher, int revents) {
    struct ev_io_http *w = (struct ev_io_http *)watcher;

    char buf_in[CLIENT_BUF_IN_SIZE];
    // TODO: view right way to use buffered recv
    int read_len = recv(w->io.fd, &buf_in, CLIENT_BUF_IN_SIZE, MSG_NOSIGNAL);
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

    char *path = (char *)malloc(strlen(w->root_dir) + sizeof("/") + strlen(buf_in) + sizeof("\0"));
    strcpy(path, w->root_dir);
    strcat(path, "/");
    strcat(path, trim(buf_in));
    if (access(path, F_OK) == 0) {
        strcpy(buf_out, "200");
    } else {
        strcpy(buf_out, "404");
    }
    send(watcher->fd, buf_out, strlen(buf_out) + 1, MSG_NOSIGNAL);
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
