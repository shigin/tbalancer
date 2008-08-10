#include <strings.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <errno.h>
#include <string.h>

#include "tpool.h"
#include "common.h"

struct tpool *make_pool(void)
{
    struct tpool *result = tb_alloc(struct tpool);
    bzero(result, sizeof(struct tpool));
    return result;
}

void free_connection(struct tconnection *conn)
{
    close(conn->sock);
    free(conn);
}

struct tconnection *make_connection(struct pool_server *server, int nonblock)
{
    struct addrinfo *res = server->res;
    struct tconnection *result;
    int sock, ret, connected=CONN_CREATED;
    sock = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    tb_debug("connection with a sock %d", sock);
    if (sock == -1)
        return NULL;
    if (nonblock)
    {
        int flags = fcntl(sock, F_GETFL, 0);
        fcntl(sock, F_SETFL, flags | O_NONBLOCK);
    }
    ret = connect(sock, res->ai_addr, res->ai_addrlen);
    if (ret != -1)
    {
        connected = CONN_CONNECTED;
        if (!nonblock)
        {
            int flags = fcntl(sock, F_GETFL, 0);
            fcntl(sock, F_SETFL, flags | O_NONBLOCK);
        }
    } else {
        if (!(nonblock && errno == EINPROGRESS))
        {
            tb_debug("can't connect (%d)");
            close(sock);
            return NULL;
        }
    }
    result = tb_alloc(struct tconnection);
    result->sock = sock;
    result->parent = server;
    result->stat = connected;
    return result;
}

void server_timeout(struct pool_server *server, int which, long msec)
{
    struct timeval *change;
    switch (which)
    {
        case TB_CONN_TO:
            if (server->c_to == 0)
                server->c_to = tb_alloc(struct timeval);
            change = server->c_to;
            break;
        case TB_WRITE_TO:
            if (server->w_to == 0)
                server->w_to = tb_alloc(struct timeval);
            change = server->w_to;
            break;
        default:
            tb_error("pool_server doesn't support %d timeout");
            return;
    }
    change->tv_sec = msec/1000;
    change->tv_usec = msec%1000;
}

struct connect_tuple
{
    struct pool_server *server;
    struct tconnection *connection;
    struct tpool *pool;
    struct event *ev;
};

void check_server(int _, short event, void *arg);
void async_connection(int fd, short event, void *arg)
{
    struct connect_tuple *tuple = (struct connect_tuple *)arg;
    int ret;
    socklen_t rlen = sizeof(ret);
    tb_debug("-> async_connection [%d]", fd);
    if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &ret, &rlen) != -1)
    {
        if (ret == 0) {
            tuple->connection->stat = CONN_CONNECTED;
            add_connection(tuple->pool, tuple->connection);
            tb_debug("<- async_connection ok [%d]", fd);
            return;
        }
    } else {
        perror("pool_connect");
    }
    tb_debug("<- async_connection failed (%d)", ret);
    free_connection(tuple->connection);
    tuple->connection = NULL;
    evtimer_set(tuple->ev, check_server, tuple);
    event_add(tuple->ev, &tuple->server->check_after);
}

void check_server(int _, short event, void *arg)
{
    struct connect_tuple *tuple = (struct connect_tuple *)arg;
    struct addrinfo *res=NULL;
    tb_debug("-> check_server %s after %ld sec", tuple->server->sname, 
            tuple->server->check_after.tv_sec);
    
    for (res = tuple->server->res0; res; res = res->ai_next)
    {
        tuple->server->res = res;
        tuple->connection = make_connection(tuple->server, 1);
        if (tuple->connection != 0)
        {
            if (tuple->connection->stat == CONN_CONNECTED)
            {
                tb_debug("<- check_server %s: connected", tuple->server->sname);
                add_connection(tuple->pool, tuple->connection);
                return;
            }
            tb_debug("<- check_server %s: async_connect", tuple->server->sname);
            event_set(tuple->ev, tuple->connection->sock, EV_WRITE,
                    async_connection, tuple);
            event_add(tuple->ev, &tuple->server->check_after);
            return;
        }
        free(tuple->connection);
    }
    tb_debug("<- check_server %s: sheduled", tuple->server->sname);
    evtimer_set(tuple->ev, check_server, tuple);
    event_add(tuple->ev, &tuple->server->check_after);
}

struct pool_server *add_server(struct tpool *pool, const char *name, uint16_t port)
{
    struct pool_server *server;
    struct tconnection *connection;
    struct addrinfo hints, *res=NULL;
    char sport[sizeof("65536")];
    int error;
    bzero(&hints, sizeof(hints));
    hints.ai_family = PF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE | AI_ADDRCONFIG;
    sprintf(sport, "%d", port);

    server = tb_alloc(struct pool_server);
    bzero(server, sizeof(server));
    server->sname = strdup(name);
    server->check_after.tv_sec = 2;
    server->c_to = NULL;
    server->w_to = NULL;
    error = getaddrinfo(name, sport, &hints, &server->res0);
    for (res = server->res0; res; res = res->ai_next)
    {
        server->res = res;
        connection = make_connection(server, 0);
        if (connection != 0)
        {
            add_connection(pool, connection);
            break;
        }
        free(connection);
    }
    if (connection == 0)
    {
        struct connect_tuple *arg = tb_alloc(struct connect_tuple);
        arg->server = server;
        arg->connection = NULL;
        arg->pool = pool;
        arg->ev = tb_alloc(struct event);
        tb_debug("make a callback for server %s", server->sname);
        evtimer_set(arg->ev, check_server, arg);
        event_add(arg->ev, &server->check_after);
        return server;
    }
    server->res = res;
    if (server->next)
    {
        server->next = pool->use_next->next;
        pool->use_next->next = server;
    } else {
        pool->use_next = server;
        pool->servers = server;
    }
    return server;
}

int add_connection(struct tpool *pool, struct tconnection *server)
{
    if (pool->last)
        pool->last->next = server;
    else
        pool->free = server;
    server->next = NULL;
    pool->last = server;
    return -1;
}

struct tconnection *get_connection(struct tpool *pool)
{
    struct tconnection *result = pool->free;
    if (pool->free)
    {
        pool->free = pool->free->next;
    } else {
        tb_debug("new connection");
        if (pool->use_next == 0)
            pool->use_next = pool->servers;
        return make_connection(pool->use_next, 1);
    }
    return result;
}
