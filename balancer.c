#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <event.h>
#include "tpool.h"

struct thrift_client {
    struct event *ev;
    struct tpool *pool;
    int origin;          /* original fd of client */
    struct tconnection *connection;   /* */
    uint32_t transmited; /* how many bytes we are recieved or transmited */
    uint32_t expect;     /* bytes to read/write */
    size_t buf_size;
    void *buffer;        /* buffer with data */
};

struct thrift_client *thrift_client_ctor(struct tpool *pool, const int origin)
{
    struct thrift_client *result;
    result = (struct thrift_client*)malloc(sizeof(struct thrift_client));
    result->origin = origin;
    result->pool = pool;
    result->ev = (struct event*)malloc(sizeof(struct event));
    result->buf_size = 0;
    result->transmited = 0;
    result->buffer = NULL;
    result->expect = 0;
    result->connection = NULL;
    return result;
}

void thrift_client_dtor(struct thrift_client *instance)
{
    free(instance->buffer);
    event_del(instance->ev);
    free(instance->ev);
    free(instance);
}

void read_len(int fd, short event, void *arg);
void write_data(int fd, short event, void *arg)
{
    struct thrift_client *tclient = arg;
    ssize_t wrote = write(fd, tclient->buffer + tclient->transmited, 
                            tclient->buf_size - tclient->transmited);
    printf("write_data\n");
    if (wrote > 0)
    {
        tclient->transmited += wrote;
        if (tclient->transmited == tclient->expect)
        {
            event_set(tclient->ev, fd, EV_READ, read_len, tclient);
        } else
            event_set(tclient->ev, fd, EV_WRITE, write_data, tclient);
        event_add(tclient->ev, NULL);
    } else {
        /* XXX fill it */
        perror("write_data");
        thrift_client_dtor(tclient);
    }
}

void read_data(int fd, short event, void *arg)
{
    struct thrift_client *tclient = arg;
    ssize_t got = read(fd, tclient->buffer + tclient->transmited, 
                           tclient->buf_size - tclient->transmited);
    printf("read_data\n");
    if (got > 0)
    {
        tclient->transmited += got;
        if (tclient->transmited == tclient->expect)
        {
            printf("data ok\n");
            tclient->transmited = 0;
            if (tclient->connection)
            {
                printf("switch to origin\n");
                add_connection(tclient->pool, tclient->connection);
                tclient->connection = NULL;
                event_set(tclient->ev, tclient->origin, 
                        EV_WRITE, write_data, tclient);
            } else {
                printf("switch to pooled\n");
                tclient->connection = get_connection(tclient->pool);
                event_set(tclient->ev, tclient->connection->sock, 
                        EV_WRITE, write_data, tclient);
            }
        } else {
            event_set(tclient->ev, fd, EV_READ, read_data, tclient);
        }
        event_add(tclient->ev, NULL);
    } else {
        /* XXX fill it */
        perror("read_data");
        thrift_client_dtor(tclient);
    }
}

void read_len(int fd, short event, void *arg)
{
    struct thrift_client *tclient = arg;
    ssize_t got;
    uint32_t len;
    fprintf(stderr, "read_len\n");
    got = read(fd, &len, sizeof(len));
    if (got == 4)
    {
        int buf_size = ntohl(len) + sizeof(len);
        if (buf_size > tclient->buf_size)
        {
            free(tclient->buffer);
            tclient->buf_size = buf_size;
            tclient->buffer = malloc(buf_size);
        }
        memcpy(tclient->buffer, &len, sizeof(len));
        tclient->expect = buf_size;
        tclient->transmited = sizeof(len);
        /* XXX check if event_set create new struct */
        event_set(tclient->ev, fd, EV_READ, read_data, tclient);
        event_add(tclient->ev, NULL);
    } else {
        /* XXX check if read 1..3 bytes */
        perror("read_len");
        close(fd);
        thrift_client_dtor(tclient);
    }
}

struct event_pair {
    struct tpool *pool;
    struct event ev;
};

void accept_client(int fd, short event, void *arg)
{
    struct event_pair *pair = (struct event_pair *)arg;
    int client;
    struct sockaddr addr;
    socklen_t len = sizeof(addr);
    /* reshedule me */
    event_add(&pair->ev, NULL);
    printf("accept\n");
    client = accept(fd, &addr, &len);
    if (client != -1)
    {
        struct thrift_client *tclient = thrift_client_ctor(pair->pool, client);
        event_set(tclient->ev, client, EV_READ, read_len, tclient);
        event_add(tclient->ev, NULL);
    } else {
        perror("accept");
    }
}

int main()
{
    struct sockaddr_in listen_on;
    struct event_pair epair;
    int ls = socket(PF_INET, SOCK_STREAM, 0);
    int one = 1;
    if (ls == -1)
    {
        perror("socket");
        return 1;
    }
    listen_on.sin_port = htons(9090);
    if (inet_aton("0.0.0.0", &listen_on.sin_addr) == -1)
    {
        perror("bind");
        return 1;
    }
    setsockopt(ls, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
    if (bind(ls, (struct sockaddr *)&listen_on, sizeof(listen_on)) == -1)
    {
        perror("bind");
        return 1;
    }

    if (listen(ls, 4) == -1)
    {
        perror("bind");
        return 1;
    }
    epair.pool = make_pool();
    add_server(epair.pool, "localhost", 9091);
    event_init();
    event_set(&epair.ev, ls, EV_READ, accept_client, &epair);
    event_add(&epair.ev, NULL);
    event_dispatch();
    return 0;
}
