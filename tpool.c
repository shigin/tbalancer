#include <strings.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>

#include "tpool.h"
#include "common.h"

struct tpool *make_pool(void)
{
    struct tpool *result = (struct tpool *)malloc(sizeof(struct tpool));
    bzero(result, sizeof(struct tpool));
    return result;
}

struct tconnection *make_connection(struct pool_server *server)
{
    struct addrinfo *res = server->res;
    struct tconnection *result;
    int sock, ret;
    sock = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    tb_debug("connection with a sock %d", sock);
    if (sock == -1)
        return NULL;
    ret = connect(sock, res->ai_addr, res->ai_addrlen);
    if (ret != -1)
    {
        int flags = fcntl(sock, F_GETFL, 0);
        fcntl(sock, F_SETFL, flags | O_NONBLOCK);
        result = (struct tconnection *)malloc(sizeof(struct tconnection));
        result->sock = sock;
        result->parent = server;
        return result;
    } else {
        close(sock);
        return NULL;
    }
}

int add_server(struct tpool *pool, const char *name, uint16_t port)
{
    struct pool_server *server;
    struct tconnection *connection;
    struct addrinfo hints, *res=NULL, *res0=NULL;
    char sport[sizeof("65536")];
    int error;
    bzero(&hints, sizeof(hints));
    hints.ai_family = PF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE | AI_ADDRCONFIG;
    sprintf(sport, "%d", port);

    server = (struct pool_server*)malloc(sizeof(struct pool_server));
    error = getaddrinfo(name, sport, &hints, &res0);
    server->res0 = res0;
    for (res = res0; res; res = res->ai_next)
    {
        server->res = res;
        connection = make_connection(server);
        if (connection != 0)
        {
            add_connection(pool, connection);
            break;
        }
        free(connection);
    }
    if (connection == 0)
        return -1;
    if (server->next)
    {
        server->next = pool->use_next->next;
        pool->use_next->next = server;
    } else {
        pool->use_next = server;
        pool->servers = server;
    }
    return 0;
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
        return make_connection(pool->use_next);
    }
    return result;
}
