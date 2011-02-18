#include <sys/socket.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <errno.h>
#include <unistd.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

const char HOST[] = "hitagi";
const char SESSION_ID[] = "wXUvnYNRy5RGx0f5lo8017dtnUIyMBja1dMhoNJ9XVuTTqNs";
const unsigned short PORT = 9091;
const int MAXSLEEP = 128;

int connect_retry(int sockfd, const struct sockaddr *addr, socklen_t alen)
{
    int nsec;
    for (nsec = 1; nsec <= MAXSLEEP; nsec <<= 1) {
        if (connect(sockfd, addr, alen) == 0) {
            printf("connected\n");
            return 0;
        }
        if (nsec <= MAXSLEEP / 2)
            sleep(nsec);
    }
    return -1;
}

int create_tcp_socket()
{
    int sockfd;
    if ((sockfd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)) < 0) {
        perror("failed to create socket");
        exit(EXIT_FAILURE);
    }
    return sockfd;
}

char *get_ip(const char *host)
{
    struct hostent *hent;
    int ip_len = 15;
    char *ip = (char *) malloc(sizeof(char) * (ip_len + 1));
    memset(ip, 0, ip_len + 1);
    if ((hent = gethostbyname(host)) == NULL) {
        herror("failed to get ip");
        exit(EXIT_FAILURE);
    }
    if ((inet_ntop(AF_INET, hent->h_addr_list[0], ip, ip_len)) == NULL) {
        perror("failed to resolve host");
        exit(EXIT_FAILURE);
    }
    return ip;
}

char *build_get_query(const char *host, const char *page)
{
    char *query;
    char *getpage;
    char *tpl = "GET /%s HTTP/1.1\r\nHost: %s\r\n\r\n"; //X-Transmission-Session-Id: %s\r\n\r\n";
    getpage = (char *) malloc(sizeof(char) * (strlen(page) + 1));
    strcpy(getpage, page);
    if (getpage[0] == '/')
        getpage++;
    query = (char *) malloc(strlen(host) + strlen(getpage) /*+ strlen(SESSION_ID)*/ + strlen(tpl) - 5);
    sprintf(query, tpl, getpage, host/*, SESSION_ID*/);
    return query;
}

char *build_post_query(const char *host, const char *page)
{
    char *query;
    char *postpage;
    char *tpl = "POST /%s HTTP/1.1\r\nHost: %s\r\nX-Transmission-Session-Id: %s\r\nContent-Length: %d\r\nContent-Type: application/json\r\n\r\n%s";
    char *json = "{\"arguments\": {\"fields\":[\"id\",\"name\",\"totalSize\"],\"ids\":[7,10]},\"method\":\"torrent-get\",\"tag\":39693}";
//    char *json = "{\"method\": \"torrent-get\"}";
    postpage = (char *) malloc(sizeof(char) * (strlen(page) + 1));
    strcpy(postpage, page);
    if (postpage[0] == '/')
        postpage++;
    query = (char *) malloc(strlen(host) + strlen(postpage) + strlen(SESSION_ID) + strlen(tpl) + 1000);
    sprintf(query, tpl, postpage, host, SESSION_ID, strlen(json), json);
    return query;
}

int main()
{
    struct sockaddr_in *remote;
    int sockfd, tmpres;
    char *ip, *get, *host, *page;
    char buf[BUFSIZ + 1];

    host = (char *) malloc(sizeof(char) * (strlen(HOST) + 1));
    strcpy(host, HOST);
    page = "/transmission/rpc";

    sockfd = create_tcp_socket();
    ip = get_ip(host);
    fprintf(stderr, "connect to %s\n", ip);
    remote = (struct sockaddr_in *) malloc(sizeof(*remote));
    remote->sin_family = AF_INET;
    tmpres = inet_pton(AF_INET, ip, &(remote->sin_addr.s_addr));
    if (tmpres < 0) {
        perror("failed to set remote->sin_addr.s_addr");
        exit(EXIT_FAILURE);
    } else if (tmpres == 0) {
        fprintf(stderr, "%s is not a valid ip address\n", ip);
        exit(EXIT_FAILURE);
    }
    remote->sin_port = htons(PORT);
    if (connect(sockfd, (struct sockaddr *)remote, sizeof(*remote)) < 0) {
        perror("failed to connect");
        exit(EXIT_FAILURE);
    }
    get = build_post_query(host, page);
    fprintf(stderr, "\n%s\n\n", get);

    int sent = 0;
    while (sent < strlen(get)) {
        tmpres = send(sockfd, get + sent, strlen(get) - sent, 0);
        if (tmpres == -1) {
            perror("failed to send query");
            exit(EXIT_FAILURE);
        }
        sent += tmpres;
    }

    memset(buf, 0, sizeof(buf));
    char *htmlcontent;
    while ((tmpres = recv(sockfd, buf, BUFSIZ, 0)) > 0) {
        htmlcontent = strstr(buf, "\r\n\r\n");
        printf("%s", buf);
        if (htmlcontent != NULL)
            break;
        memset(buf, 0, sizeof(buf));
    }
    printf("\n");

    if (tmpres < 0) {
        perror("error receiving data");
    }

    free(get);
    free(remote);
    free(ip);

    close(sockfd);

    return 0;
}
