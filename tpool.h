#ifndef TPOOL_H__
#define TPOOL_H__
#include <stdint.h>

#define CONN_CREATED 1
#define CONN_CONNECTED 2
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
    int stat;
    struct pool_server *parent;
    struct tconnection *next;
};
void free_connection(struct tconnection *conn);

struct tpool
{
    struct pool_server *servers;
    struct pool_server *use_next;
    struct tconnection *free;
    struct tconnection *last;
};

/** pool
 * 
 * get_connection -> work with connection -> add_connection
 */
struct tpool *make_pool();
/**
 *  Adds server to pool. 
 *
 *  The routine adds a server to a pool. It checks if server is alive by creating 
 *  new connection (and adds one to the pool.
 *
 *  WARNING! The routine sends a DNS query.
 *  WARNING! The routine can block.
 */
int add_server(struct tpool *pool, const char *name, uint16_t port);
/**
 * Add connection to the pool
 */
int add_connection(struct tpool *pool, struct tconnection *server);
/**
 * Get connection from the pool.
 *
 * Connection can be in a two states:
 *   - connected;
 *   - created.
 *
 * If connection created you should check async for connection to complete.
 */
struct tconnection *get_connection(struct tpool *pool);
#endif
