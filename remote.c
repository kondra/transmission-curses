#include <sys/socket.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <errno.h>
#include <unistd.h>

#include <stdio.h>
#include <assert.h>
#include <stdlib.h>
#include <string.h>

#include "remote.h"

static const char HOST[] = "hitagi";
static const char PAGE[] = "transmission/rpc";
static const unsigned short PORT = 9091;

static char *SESSION_ID = NULL;

enum {
    HTTP_CONFLICT = 409,
    HTTP_OK = 200,
};

typedef struct {
    int status_code;
    char *status_str;
    int length;
    char *session_id;
} HeaderInfo;

static char *get_str(char *p, char b, char e);
static HeaderInfo *header_parser(char *header);
static void header_destroy(HeaderInfo *info);
static int create_tcp_socket();
static char *get_ip(const char *host);
static int remote_connect(const char *host, const int port);
static void remote_disconnect(int sockfd);
static char *build_header(const char *host, const char *page, const int len);
static void send_query(int sockfd, const char *header, const char *json_query);
static char *get_response(int sockfd, HeaderInfo **info);
static char *get_json_data(char *response);

static char *get_str(char *p, char b, char e)
{
    char *pe, *str;
    while (*p != b) 
        p++;
    pe = ++p;
    while (*pe != e) 
        pe++;
    *pe = '\0';
    str = (char *) calloc(strlen(p) + 1, sizeof(char));
    strcpy(str, p);
    *pe = e;
    return str;
}

static HeaderInfo *header_parser(char *header)
{
    char *p, *needle;
    HeaderInfo *info;

    info = (HeaderInfo *) calloc(1, sizeof(*info));
    assert(info != 0);

    needle = "HTTP/1.1";
    p = strstr(header, needle);
    assert(p != NULL);
    p += strlen(needle);
    sscanf(p, "%d", &info->status_code);
    info->status_str = get_str(p, ' ', '\r');

    needle = "X-Transmission-Session-Id:";
    p = strstr(header, needle);
    if (p != NULL) {
        p += strlen(needle);
        info->session_id = get_str(p, ' ', '\r');
    } else {
        info->session_id = NULL;
    }

    needle = "Content-Length: ";
    p = strstr(header, needle);
    assert(p != NULL);
    p += strlen(needle);
    sscanf(p, "%d", &info->length);
/*
    printf("status code: %d\n", info->status_code);
    printf("status str: %s\n", info->status_str);
    printf("length: %d\n", info->length);
    printf("session id: %s\n", info->session_id);
*/
    return info;
}

static void header_destroy(HeaderInfo *info)
{
    free(info->session_id);
    free(info->status_str);
}

static int create_tcp_socket()
{
    int sockfd;
    if ((sockfd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)) < 0) {
        perror("failed to create socket");
        exit(EXIT_FAILURE);
    }
    return sockfd;
}

static char *get_ip(const char *host)
{
    struct hostent *hent;
    int ip_len = 15;
    char *ip = (char *) calloc(ip_len + 1, sizeof(char));
    assert(ip != NULL);
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

static int remote_connect(const char *host, const int port)
{
    struct sockaddr_in *remote;
    int sockfd, tmpres;
    char *ip;

    sockfd = create_tcp_socket();
    ip = get_ip(host);
    fprintf(stderr, "connect to %s\n", ip);

    remote = (struct sockaddr_in *) calloc(1, sizeof(*remote));
    assert(remote != NULL);

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

    free(ip);

    return sockfd;
}

static void remote_disconnect(int sockfd)
{
    close(sockfd);
}

static char *build_header(const char *host, const char *page, const int len)
{
    char *header;
    char buf[32]; //for int
    const char *header_format = "POST /%s HTTP/1.1\r\nHost: %s\r\nX-Transmission-Session-Id: %s\r\nContent-Length: %s\r\nContent-Type: application/json\r\n\r\n";

    if (page[0] == '/') {
        fprintf(stderr, "leading \"/\" in page argument\n");
        exit(EXIT_FAILURE);
    }

    sprintf(buf, "%d", len);
    header = (char *) calloc(strlen(host) + strlen(page) + strlen(SESSION_ID) + strlen(header_format) - 8 + strlen(buf), sizeof(char));
    assert(header != NULL);
    sprintf(header, header_format, page, host, SESSION_ID, buf);

    return header;
}

static void send_query(int sockfd, const char *header, const char *json_query)
{
    char *data;
    int sent = 0;
    int tmpres, len;

    data = (char *) calloc(strlen(header) + strlen(json_query), sizeof(char));
    assert(data != NULL);
    strcpy(data, header);
    strcat(data, json_query);
    len = strlen(data);

    while (sent < len) {
        tmpres = send(sockfd, data + sent, len - sent, 0);
        if (tmpres < 0) {
            perror("failed to send query");
            exit(EXIT_FAILURE);
        }
        sent += tmpres;
    }

    free(data);
}

static char *get_response(int sockfd, HeaderInfo **info)
{
    char buf[BUFSIZ + 1];
    char *response;
    int tmpres;
    int size = BUFSIZ + 1;
    int cpos = 0;
    int flag = 0;

    response = (char *) calloc(BUFSIZ + 1, sizeof(char));
    assert(response != NULL);

    while ((tmpres = recv(sockfd, buf, BUFSIZ, 0)) > 0) {
        if (!flag) {
            *info = header_parser(buf);
            flag = 1;
//            fprintf(stderr, "%s\n", (*info)->status_str);
            if ((*info)->status_code == HTTP_CONFLICT)
                return NULL;
        }
        if (size - cpos > tmpres) {
            strncpy(response + cpos, buf, tmpres);
        } else {
            response = (char *) realloc(response, (size = cpos + tmpres));
            assert(response != NULL);
            strncpy(response + cpos, buf, tmpres);
        }
        cpos += tmpres;
        if (cpos >= (*info)->length)
            break;
    }

    if (tmpres < 0) {
        perror("error receiving data");
        exit(EXIT_FAILURE);
    }

    return response;
}

static char *get_json_data(char *response)
{
    char *p, *pe, *data;
    char *needle = "\r\n\r\n";
    p = strstr(response, needle);
    p += strlen(needle);

    pe = strstr(response, needle);
    *pe = '\0';

    data = (char *) calloc(strlen(p), sizeof(char));
    strcpy(data, p);

    return data;
}

char *json_exchange(const char *json_query)
{
    int sockfd;
    char *header, *response, *json_data;
    HeaderInfo *info;

    if (SESSION_ID == NULL) {
        SESSION_ID = (char *) calloc(50, sizeof(char));
        assert(SESSION_ID != NULL);
    }

    sockfd = remote_connect(HOST, PORT);
    while (1) {
        header = build_header(HOST, PAGE, strlen(json_query));
        send_query(sockfd, header, json_query);
        response = get_response(sockfd, &info);
        if (info->status_code == HTTP_CONFLICT) {
            assert(strlen(info->session_id) < 50);
            strcpy(SESSION_ID, info->session_id);
            header_destroy(info);
            free(response);
            continue;
        }
        if (info->status_code == HTTP_OK) {
            header_destroy(info);
            break;
        }
    }
    remote_disconnect(sockfd);

    json_data = get_json_data(response);
    free(response);

    return json_data;
}
