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

struct connect_tuple
{
    struct tb_server *server;
    struct tb_connection *connection;
    struct tb_pool *pool;
    struct event *ev;
};

void check_server(int _, short event, void *arg);
void async_connection(int fd, short event, void *arg);
void shedule_check(struct tb_pool *pool, struct tb_server *server);

struct tb_pool *make_pool(void)
{
    struct tb_pool *result = tb_alloc(struct tb_pool);
    bzero(result, sizeof(*result));
    return result;
}

void pool_info(struct tb_pool *pool)
{
    struct tb_server *iter;
    tb_debug("ii pool servers at %p (%p next)", pool->servers, pool->use_next);
    for (iter=pool->servers; iter != NULL; iter = iter->next)
        tb_debug("ii server %p is %s:%d", iter, iter->sname, iter->port);
}

void free_connection(struct tb_connection *conn)
{
    close(conn->sock);
    conn->parent->total -= 1;
    free(conn);
}

void dead_connection(struct tb_pool *pool, struct tb_connection *conn)
{
    struct tb_server *server = conn->parent;
    pool_info(pool);
    if (server->stat != TB_SERVER_OK)
    {
        struct tb_connection *connection;
        tb_debug("-> dead_connection: %s:%d dead, schedule check", 
            server->sname, server->port);
        for (connection = server->connection; connection; 
                connection = connection->next)
        {
            free_connection(conn);
        }
        server->stat = TB_SERVER_DEAD;
        shedule_check(pool, server);
        /* remove the server from aviable list */
        if (server->prev)
            server->prev->next = server->next;
        if (server->next)
            server->next->prev = server->prev;
        if (pool->servers == server)
            pool->servers = server->next;
        if (pool->use_next == server)
        {
            pool->use_next = server->next;
            if (pool->use_next == NULL)
                pool->use_next = pool->servers;
        }
        /* server hasn't prev and next server now */
        server->next = NULL;
        server->prev = NULL;
    } else {
        server->stat = TB_SERVER_UNKNOWN;
        tb_debug("-> dead_connection: %s:%d unknown status", 
            server->sname, server->port);
        free_connection(conn);
    }
    tb_debug("<- dead_connection: %s:%d status == %d", 
        server->sname, server->port, server->stat);
}

void server_to_pool(struct tb_pool *pool, struct tb_server *server)
{
    tb_debug("-> server_to_pool: %s:%d", server->sname, server->port);
    if (pool->use_next)
    {
        server->next = pool->use_next;
        server->prev = pool->use_next->prev;
        if (server->prev)
            server->prev->next = server;
        pool->use_next->prev = server;
        /* if old use_next was first server adjust servers */
        if (pool->use_next == pool->servers)
            pool->servers = server;
        pool->use_next = server;
    } else {
        pool->use_next = server;
        pool->servers = server;
    }
    tb_debug("<- server_to_pool: %s:%d", server->sname, server->port);
}

struct tb_connection *make_connection(struct tb_server *server, int nonblock)
{
    struct addrinfo *res = server->res;
    struct tb_connection *result;
    int sock, ret, connected=CONN_CREATED;
    sock = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    tb_debug("-> make_connection: socket %d", sock);
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
            tb_debug("<- make_connection: can't connect %d (errno %d)",
                sock, errno);
            close(sock);
            return NULL;
        }
    }
    result = tb_alloc(struct tb_connection);
    result->sock = sock;
    result->parent = server;
    result->stat = connected;
    tb_debug("<- make_connection: done %d", sock);
    return result;
}

void server_timeout(struct tb_server *server, int which, long msec)
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
            tb_error("tb_server doesn't support %d timeout");
            return;
    }
    change->tv_sec = msec/1000;
    change->tv_usec = msec%1000;
}

void async_connection(int fd, short event, void *arg)
{
    struct connect_tuple *tuple = (struct connect_tuple *)arg;
    int ret;
    socklen_t rlen = sizeof(ret);
    tb_debug("-> async_connection [%d]", fd);
    if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &ret, &rlen) != -1)
    {
        if (ret == 0) {
            tuple->connection->parent->stat = TB_SERVER_OK;
            tuple->connection->stat = CONN_CONNECTED;
            add_connection(tuple->pool, tuple->connection);
            server_to_pool(tuple->pool, tuple->server);
            free(tuple->ev);
            free(tuple);
            tb_debug("<- async_connection ok [%d]", fd);
            return;
        }
    } else {
        perror("async_connection");
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
    tb_debug("-> check_server %s:%d after %ld sec", tuple->server->sname, 
            tuple->server->port, tuple->server->check_after.tv_sec);
    
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
                server_to_pool(tuple->pool, tuple->server);
                free(tuple->ev);
                free(tuple);
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
    tb_debug("<- check_server %s:%d sheduled", 
        tuple->server->sname, tuple->server->sname);
    evtimer_set(tuple->ev, check_server, tuple);
    event_add(tuple->ev, &tuple->server->check_after);
}

struct tb_server *add_server(struct tb_pool *pool, const char *name, uint16_t port)
{
    struct tb_server *server;
    struct tb_connection *connection;
    struct addrinfo hints, *res=NULL;
    char sport[sizeof("65536")];
    int error;
    bzero(&hints, sizeof(hints));
    hints.ai_family = PF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE | AI_ADDRCONFIG;
    sprintf(sport, "%d", port);

    server = tb_alloc(struct tb_server);
    bzero(server, sizeof(*server));
    server->sname = strdup(name);
    server->port = port;
    server->check_after.tv_sec = 2;

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
        shedule_check(pool, server);
    } else {
        server_to_pool(pool, server);
    }
    return server;
}

void shedule_check(struct tb_pool *pool, struct tb_server *server)
{
    struct connect_tuple *arg = tb_alloc(struct connect_tuple);
    tb_debug("-> shedule_check: server %s:%d",
        server->sname, server->port);
    arg->server = server;
    arg->connection = NULL;
    arg->pool = pool;
    arg->ev = tb_alloc(struct event);
    evtimer_set(arg->ev, check_server, arg);
    event_add(arg->ev, &server->check_after);
    tb_debug("<- shedule_check: server %s:%d",
        server->sname, server->port);
}

int add_connection(struct tb_pool *pool, struct tb_connection *connection)
{
    connection->parent->free += 1;
    connection->next = connection->parent->connection;
    connection->parent->connection = connection;
    return 0;
}

struct tb_connection *get_connection(struct tb_pool *pool)
{
    struct tb_connection *result;
    pool_info(pool);
    if (pool->use_next == NULL)
    {
        tb_debug("!! get_connection: no servers");
        return NULL;
    }
    if (pool->use_next->connection)
    {
        tb_debug("-> get_connection: get exist");
        result = pool->use_next->connection;
        pool->use_next->connection = result->next;
        result->next = NULL;
    } else {
        tb_debug("-> get_connection: new connection");
        result = make_connection(pool->use_next, 1);
    }
    pool->use_next = pool->use_next->next;
    if (pool->use_next == 0)
    {
        pool->use_next = pool->servers;
    }
    tb_debug("<- get_connection: %d -- %s:%d",
        result->sock, result->parent->sname, result->parent->port);
    return result;
}
