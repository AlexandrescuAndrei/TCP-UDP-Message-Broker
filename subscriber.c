#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/select.h>
#include <fcntl.h>
#include <errno.h>
#include <stdint.h>
#include <signal.h>

#define BUFLEN 1600
#define ID_LEN 11

void usage(char *prog_name) {
    fprintf(stderr, "Usage: %s <ID_CLIENT> <IP_SERVER> <PORT_SERVER>\n", prog_name);
    exit(1);
}

void send_all(int sockfd, char *buf, int len) {
    int sent = 0;
    while (sent < len) {
        int ret = send(sockfd, buf + sent, len - sent, 0);
        if (ret < 0) {
            perror("send");
            exit(1);
        }
        sent += ret;
    }
}

void send_framed(int sockfd, const char *msg) {
    uint32_t len = strlen(msg);
    uint32_t net_len = htonl(len);
    send_all(sockfd, (char *)&net_len, sizeof(net_len));
    send_all(sockfd, (char *)msg, len);
}

int recv_all(int sockfd, char *buf, int len) {
    int received = 0;
    while (received < len) {
        int ret = recv(sockfd, buf + received, len - received, 0);
        if (ret <= 0) return ret;
        received += ret;
    }
    return received;
}

int recv_framed(int sockfd, char *buf) {
    uint32_t net_len;
    int ret = recv_all(sockfd, (char *)&net_len, sizeof(uint32_t));
    if (ret <= 0) return ret;
    uint32_t len = ntohl(net_len);
    return recv_all(sockfd, buf, len);
}

int main(int argc, char *argv[]) {
    FILE *f2 = fopen("error2.txt", "w");
    if (argc != 4) usage(argv[0]);

    setvbuf(stdout, NULL, _IONBF, BUFSIZ);
    signal(SIGPIPE, SIG_IGN);

    char id[ID_LEN];
    strncpy(id, argv[1], ID_LEN - 1);
    id[ID_LEN - 1] = '\0';

    int sockfd;
    struct sockaddr_in serv_addr;

    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) perror("socket");

    int flag = 1;
    setsockopt(sockfd, IPPROTO_TCP, TCP_NODELAY, &flag, sizeof(int));

    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(atoi(argv[3]));
    inet_aton(argv[2], &serv_addr.sin_addr);

    if (connect(sockfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0)
        perror("connect");

    send_all(sockfd, id, ID_LEN - 1);

    fd_set read_fds;
    FD_ZERO(&read_fds);
    int fdmax = sockfd > STDIN_FILENO ? sockfd : STDIN_FILENO;

    while (1) {
        FD_SET(STDIN_FILENO, &read_fds);
        FD_SET(sockfd, &read_fds);

        if (select(fdmax + 1, &read_fds, NULL, NULL, NULL) < 0)
            perror("select");

        if (FD_ISSET(STDIN_FILENO, &read_fds)) {
            char input[BUFLEN];
            fgets(input, BUFLEN, stdin);

            if (strncmp(input, "exit", 4) == 0) {
                close(sockfd);
                return 0;
            }

            char *cmd = strtok(input, " \n");
            if (!cmd) continue;

            if (strcmp(cmd, "subscribe") == 0) {
                char *topic = strtok(NULL, " \n");
                if (!topic) continue;

                char msg[BUFLEN];
                snprintf(msg, sizeof(msg), "subscribe %s", topic);
                send_framed(sockfd, msg);
                printf("Subscribed to topic %s\n", topic);
            } else if (strcmp(cmd, "unsubscribe") == 0) {
                char *topic = strtok(NULL, " \n");
                if (!topic) continue;

                char msg[BUFLEN];
                snprintf(msg, sizeof(msg), "unsubscribe %s", topic);
                send_framed(sockfd, msg);
                printf("Unsubscribed from topic %s\n", topic);
            }
        }

        if (FD_ISSET(sockfd, &read_fds)) {
            char msg[BUFLEN];
            int n = recv_framed(sockfd, msg);
            if (n <= 0) {
                close(sockfd);
                return 0;
            }
            msg[n] = '\0';
            fprintf(f2, "%s\n", msg);
            printf("%s\n", msg);
        }
    }

    return 0;
}
