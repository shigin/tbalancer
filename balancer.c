#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <event.h>
#include <assert.h>
#include "tpool.h"
#include "common.h"

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
    int flags = fcntl(origin, F_GETFL, 0);
    fcntl(origin, F_SETFL, flags | O_NONBLOCK);
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
    tb_debug("-> write_data [%d]", fd);
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
    tb_debug("<- write_data [%d]", fd);
}

void pool_connect(int fd, short event, void *arg)
{
    struct thrift_client *tclient = arg;
    int ret;
    socklen_t rlen = sizeof(ret);
    tb_debug("-> pool_connect [%d]", fd);
    if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &ret, &rlen) != -1)
    {
        if (ret == 0) {
            tclient->connection->stat = CONN_CONNECTED;
            event_set(tclient->ev, fd, EV_WRITE, write_data, tclient);
            event_add(tclient->ev, NULL);
            tb_debug("<- pool_connect [%d]", fd);
            return;
        }
    } else {
        perror("pool_connect");
    }
    /* XXX make pool now about broken connect */
    free_connection(tclient->connection);
    /* get rid of a copy-paste */
    tclient->connection = get_connection(tclient->pool);
    ret = tclient->connection->stat;
    if (ret == CONN_CONNECTED)
    {
        event_set(tclient->ev, fd, EV_WRITE, write_data, tclient);
    } else {
        event_set(tclient->ev, fd, EV_WRITE, pool_connect, tclient);
    }
    event_add(tclient->ev, NULL);
    tb_debug("<- pool_connect [%d]", fd);
}

void read_data(int fd, short event, void *arg)
{
    struct thrift_client *tclient = arg;
    struct timeval *to = NULL;
    ssize_t got = read(fd, tclient->buffer + tclient->transmited, 
                           tclient->buf_size - tclient->transmited);
    tb_debug("read_data [%d]", fd);
    if (got > 0)
    {
        tclient->transmited += got;
        if (tclient->transmited == tclient->expect)
        {
            tb_debug("data ok");
            tclient->transmited = 0;
            if (tclient->connection)
            {
                tb_debug("switch to origin");
                add_connection(tclient->pool, tclient->connection);
                tclient->connection = NULL;
                event_set(tclient->ev, tclient->origin, 
                        EV_WRITE, write_data, tclient);
            } else {
                tclient->connection = get_connection(tclient->pool);
                tb_debug("switch to pooled %d [stat %d]", 
                    tclient->connection->sock, tclient->connection->stat);
                if (tclient->connection->stat == CONN_CONNECTED)
                {
                    event_set(tclient->ev, tclient->connection->sock, 
                            EV_WRITE, write_data, tclient);
                    to = &tclient->connection->w_to;
                } else {
                    tb_debug("wait to connect for %d", tclient->connection->sock);
                    event_set(tclient->ev, tclient->connection->sock, 
                            EV_WRITE, pool_connect, tclient);
                    to = &tclient->connection->w_to;
                }
            }
        } else {
            event_set(tclient->ev, fd, EV_READ, read_data, tclient);
        }
        event_add(tclient->ev, to);
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
    tb_debug("-> read_len [%d]", fd);
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
        if (got != 0) {
            perror("read_len");
        } else {
            tb_debug("close connection %d", fd);
        }
        close(fd);
        thrift_client_dtor(tclient);
    }
    tb_debug("<- read_len [%d]", fd);
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
    tb_debug("accept [%d]", fd);
    client = accept(fd, &addr, &len);
    if (client != -1)
    {
        struct thrift_client *tclient = thrift_client_ctor(pair->pool, client);
        tb_debug("new client %d", client);
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
    struct pool_server* server;
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
    event_init();
    epair.pool = make_pool();
    server = add_server(epair.pool, "localhost", 9091);
    assert(server);
    server_timeout(server, TB_CONN_TO, 3);
    server_timeout(server, TB_WRITE_TO, 3);
    event_set(&epair.ev, ls, EV_READ, accept_client, &epair);
    event_add(&epair.ev, NULL);
    event_dispatch();
    return 0;
}
