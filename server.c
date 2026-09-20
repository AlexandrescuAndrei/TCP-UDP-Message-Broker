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
#include <signal.h>
#include <stdint.h>
#include <math.h>

#define BUFLEN 1600
#define MAX_CLIENTS 100
#define TOPIC_LEN 50
#define ID_LEN 11
#define CONTENT_LEN 1501

typedef struct {
    char ip[16];
    uint16_t port;
    char topic[TOPIC_LEN + 1];
    uint8_t type;
    char payload[CONTENT_LEN];
} udp_message;

typedef struct stored_msg {
    int len;
    char *data;
    struct stored_msg *next;
} stored_msg;

typedef struct subscription {
    char topic[TOPIC_LEN + 1];
    struct subscription *next;
} subscription;

typedef struct client {
    char id[ID_LEN];
    int sockfd;
    int connected;
    struct sockaddr_in addr;
    subscription *subs;
    stored_msg *pending;
} client;

client clients[MAX_CLIENTS];
int client_count = 0;
fd_set read_fds, tmp_fds;
int fdmax;

void usage(char *prog_name) {
    fprintf(stderr, "Usage: %s <PORT>\n", prog_name);
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

int recv_all(int sockfd, char *buf, int len) {
    int received = 0;
    while (received < len) {
        int ret = recv(sockfd, buf + received, len - received, 0);
        if (ret <= 0) return ret;
        received += ret;
    }
    return received;
}

void send_framed(int sockfd, void *data, uint32_t len) {
    uint32_t net_len = htonl(len);
    send_all(sockfd, (char *)&net_len, sizeof(uint32_t));
    send_all(sockfd, data, len);
}

int recv_framed(int sockfd, char *buffer) {
    uint32_t net_len;
    int ret = recv_all(sockfd, (char *)&net_len, sizeof(uint32_t));
    if (ret <= 0) return ret;
    uint32_t len = ntohl(net_len);
    return recv_all(sockfd, buffer, len);
}

udp_message parse_udp_message(char *buffer, struct sockaddr_in *src_addr) {
    udp_message msg;
    memset(&msg, 0, sizeof(msg));

    strncpy(msg.ip, inet_ntoa(src_addr->sin_addr), 15);
    msg.ip[15] = '\0';
    msg.port = ntohs(src_addr->sin_port);

    strncpy(msg.topic, buffer, 50);
    msg.topic[50] = '\0';

    msg.type = buffer[50];

    memcpy(msg.payload, buffer + 51, 1500);

    return msg;
}

int match_topic(const char *pattern, const char *topic) {
    if (strcmp(pattern, topic) == 0)
        return 1;
    return 0;
}

client *get_client_by_id(char *id) {
    for (int i = 0; i < client_count; i++) {
        if (strcmp(clients[i].id, id) == 0) {
            return &clients[i];
        }
    }
    return NULL;
}

void store_message(client *c, const char *buf, int len) {
    stored_msg *msg = malloc(sizeof(stored_msg));
    msg->data = malloc(len);
    memcpy(msg->data, buf, len);
    msg->len = len;
    msg->next = NULL;

    if (!c->pending) {
        c->pending = msg;
    } else {
        stored_msg *curr = c->pending;
        while (curr->next) curr = curr->next;
        curr->next = msg;
    }
}

void send_pending_messages(client *c) {
    stored_msg *curr = c->pending;
    while (curr) {
        send_framed(c->sockfd, curr->data, curr->len);
        stored_msg *tmp = curr;
        curr = curr->next;
        free(tmp->data);
        free(tmp);
    }
    c->pending = NULL;
}

void add_subscription(client *c, char *topic) {
    subscription *curr = c->subs;
    while (curr) {
        if (strcmp(curr->topic, topic) == 0) return;
        curr = curr->next;
    }
    subscription *s = malloc(sizeof(subscription));
    strncpy(s->topic, topic, TOPIC_LEN);
    s->next = c->subs;
    c->subs = s;
}

void remove_subscription(client *c, char *topic) {
    subscription *prev = NULL, *curr = c->subs;
    while (curr) {
        if (strcmp(curr->topic, topic) == 0) {
            if (prev) prev->next = curr->next;
            else c->subs = curr->next;
            free(curr);
            return;
        }
        prev = curr;
        curr = curr->next;
    }
}

int main(int argc, char *argv[]) {
    FILE *f = fopen("error.txt", "w");
    if (argc != 2) usage(argv[0]);

    setvbuf(stdout, NULL, _IONBF, BUFSIZ);
    signal(SIGPIPE, SIG_IGN);

    int tcp_sock, udp_sock, newsock;
    struct sockaddr_in serv_addr, cli_addr;
    socklen_t clilen = sizeof(cli_addr);
    int port = atoi(argv[1]);

    tcp_sock = socket(AF_INET, SOCK_STREAM, 0);
    udp_sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (tcp_sock < 0 || udp_sock < 0) perror("socket");

    int flag = 1;
    setsockopt(tcp_sock, IPPROTO_TCP, TCP_NODELAY, &flag, sizeof(int));

    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(port);
    serv_addr.sin_addr.s_addr = INADDR_ANY;

    bind(tcp_sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr));
    bind(udp_sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr));

    listen(tcp_sock, MAX_CLIENTS);

    FD_ZERO(&read_fds);
    FD_SET(STDIN_FILENO, &read_fds);
    FD_SET(tcp_sock, &read_fds);
    FD_SET(udp_sock, &read_fds);
    fdmax = udp_sock > tcp_sock ? udp_sock : tcp_sock;

    while (1) {
        tmp_fds = read_fds;
        if (select(fdmax + 1, &tmp_fds, NULL, NULL, NULL) < 0)
            perror("select");

        for (int i = 0; i <= fdmax; i++) {
            if (!FD_ISSET(i, &tmp_fds)) continue;

            if (i == STDIN_FILENO) {
                char cmd[20];
                fgets(cmd, sizeof(cmd), stdin);
                if (strncmp(cmd, "exit", 4) == 0) {
                    for (int j = 0; j < client_count; j++) {
                        if (clients[j].connected)
                            close(clients[j].sockfd);
                    }
                    close(tcp_sock);
                    close(udp_sock);
                    return 0;
                }
            } else if (i == tcp_sock) {
                newsock = accept(tcp_sock, (struct sockaddr *)&cli_addr, &clilen);
                char id[ID_LEN];
                recv_all(newsock, id, ID_LEN - 1);
                id[ID_LEN - 1] = '\0';

                client *existing = get_client_by_id(id);
                if (existing && existing->connected) {
                    printf("Client %s already connected.\n", id);
                    close(newsock);
                } else {
                    if (!existing) {
                        client c;
                        memset(&c, 0, sizeof(c));
                        strncpy(c.id, id, ID_LEN);
                        c.sockfd = newsock;
                        c.connected = 1;
                        c.addr = cli_addr;
                        clients[client_count++] = c;
                        existing = &clients[client_count - 1];
                    } else {
                        existing->sockfd = newsock;
                        existing->connected = 1;
                    }

                    FD_SET(newsock, &read_fds);
                    if (newsock > fdmax) fdmax = newsock;

                    printf("New client %s connected from %s:%d.\n",
                           id, inet_ntoa(cli_addr.sin_addr), ntohs(cli_addr.sin_port));

                    send_pending_messages(existing);
                }
            } else if (i == udp_sock) {
                udp_message msg;
                struct sockaddr_in src_addr;
                socklen_t addrlen = sizeof(src_addr);
                char receive_buffer[BUFLEN];
                recvfrom(udp_sock, &receive_buffer, BUFLEN, 0, (struct sockaddr *)&src_addr, &addrlen);
                msg = parse_udp_message(receive_buffer, &src_addr);
                fprintf(f, "%s\n", msg.topic);

                char send_buf[BUFLEN];
                int len = snprintf(send_buf, sizeof(send_buf), "%s:%d - %s - ",
                                   inet_ntoa(src_addr.sin_addr), ntohs(src_addr.sin_port),
                                   msg.topic);

                switch (msg.type) {
                    case 0: {
                        uint32_t num;
                        memcpy(&num, msg.payload + 1, sizeof(uint32_t));
                        num = ntohl(num);
                        if (msg.payload[0]) num *= -1;
                        len += sprintf(send_buf + len, "INT - %d", num);
                        break;
                    }
                    case 1: {
                        uint16_t sh;
                        memcpy(&sh, msg.payload, sizeof(uint16_t));
                        sh = ntohs(sh);
                        len += sprintf(send_buf + len, "SHORT_REAL - %.2f", sh / 100.0);
                        break;
                    }
                    case 2: {
                        uint32_t nr;
                        uint8_t power;
                        memcpy(&nr, msg.payload + 1, sizeof(uint32_t));
                        memcpy(&power, msg.payload + 5, sizeof(uint8_t));
                        nr = ntohl(nr);
                        double val = nr / pow(10, power);
                        if (msg.payload[0]) val *= -1;
                        len += sprintf(send_buf + len, "FLOAT - %.*f", power, val);
                        break;
                    }
                    case 3: {
                        len += sprintf(send_buf + len, "STRING - %s", msg.payload);
                        break;
                    }
                    default:
                        break;
                }

                for (int j = 0; j < client_count; j++) {
                    subscription *s = clients[j].subs;
                    while (s) {
                        if (match_topic(s->topic, msg.topic)) {
                            fprintf(f, "A dat match");
                            if (clients[j].connected) {
                                fprintf(f, "%s\n", send_buf);
                                send_framed(clients[j].sockfd, send_buf, strlen(send_buf) + 1);
                            }
                            break;
                        }
                        s = s->next;
                    }
                }
            } else {
                char buf[BUFLEN];
                int n = recv_framed(i, buf);
                if (n <= 0) {
                    for (int j = 0; j < client_count; j++) {
                        if (clients[j].sockfd == i) {
                            printf("Client %s disconnected.\n", clients[j].id);
                            close(i);
                            clients[j].connected = 0;
                            FD_CLR(i, &read_fds);
                            break;
                        }
                    }
                } else {
                    buf[n] = '\0';
                    fprintf(f, "%s\n", buf);
                    char *cmd = strtok(buf, " \n");
                    char *topic = strtok(NULL, " \n");
                    if (!cmd || !topic) continue;

                    for (int j = 0; j < client_count; j++) {
                        if (clients[j].sockfd == i) {
                            if (strcmp(cmd, "subscribe") == 0) {
                                add_subscription(&clients[j], topic);
                            } else if (strcmp(cmd, "unsubscribe") == 0) {
                                remove_subscription(&clients[j], topic);
                            }
                        }
                    }
                }
            }
        }
    }

    return 0;
}
