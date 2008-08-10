#ifndef TPOOL_H__
#define TPOOL_H__
#include <stdint.h>
#include <event.h>

#define CONN_CREATED 1
#define CONN_CONNECTED 2

#define TB_CONN_TO 0 /* connection timeout */
#define TB_READ_TO 1 /* read timeout */
#define TB_WRITE_TO 2 /* write timeout */

struct tb_server
{
    char *sname;
    uint16_t port;
    struct tb_server *next;
    struct addrinfo *res0, *res;
    struct timeval *c_to, *w_to, check_after;
};

struct tb_connection
{
    int sock;
    int stat;
    struct tb_server *parent;
    struct tb_connection *next;
    struct timeval c_to, w_to;
};
void free_connection(struct tb_connection *conn);

struct tb_pool
{
    struct tb_server *servers;
    struct tb_server *use_next;
    struct tb_connection *free;
    struct tb_connection *last;
};

/**
 * The routine should be called if connection is dead or fails.
 */
void dead_connection(struct tb_pool *pool, struct tb_connection *conn);
/** pool
 * 
 * get_connection -> work with connection -> add_connection
 */
struct tb_pool *make_pool();
/**
 *  Adds server to pool. 
 *
 *  The routine adds a server to a pool. It checks if server is alive by creating 
 *  new connection (and adds one to the pool.
 *
 *  WARNING! The routine sends a DNS query.
 *  WARNING! The routine can block.
 *  WARNING! The routine make a callback if server is unaviable.
 */
struct tb_server *add_server(struct tb_pool *pool, const char *name, uint16_t port);

void server_timeout(struct tb_server *server, int which, long msec);
/**
 * Add connection to the pool
 */
int add_connection(struct tb_pool *pool, struct tb_connection *server);
/**
 * Get connection from the pool.
 *
 * Connection can be in a two states:
 *   - connected;
 *   - created.
 *
 * If connection created you should check async for connection to complete.
 */
struct tb_connection *get_connection(struct tb_pool *pool);
#endif
