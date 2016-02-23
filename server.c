#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>

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

int main(int argc, char *const argv[]) {
    char *ip;
    int port = 0;
    char *dir;

    if (parse_cli_args(argc, argv, &ip, &port, &dir) != 0) {
        printf("Invalid args\n");
        return 1;
    }

    printf("host: %s\n", ip);
    printf("port: %d\n", port);
    printf("dir: %s\n", dir);

    free(ip);
    free(dir);

    return 0;
}
