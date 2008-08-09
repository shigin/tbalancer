#ifndef TPOOL_H__
#define TPOOL_H__
#include <stdint.h>

struct pool_server
{
    char *sname;
    uint16_t port;
    struct pool_server *next;
    struct addrinfo *res0, *res;
};

struct tconnection
{
    int sock;
    struct pool_server *parent;
    struct tconnection *next;
};

struct tpool
{
    struct pool_server *servers;
    struct pool_server *use_next;
    struct tconnection *free;
    struct tconnection *last;
};

/** pool
 * 
 * eject_server -> work with server -> add_server
 */
struct tpool *make_pool();
/**
 *  Adds server to pool. 
 *
 *  WARNING! The routine send a DNS query.
 */
int add_server(struct tpool *pool, const char *name, uint16_t port);
int add_connection(struct tpool *pool, struct tconnection *server);
struct tconnection *get_connection(struct tpool *pool);
#endif
