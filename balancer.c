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
#include <errno.h>
#include "tpool.h"
#include "common.h"

struct thrift_client {
    struct event *ev;
    struct tb_pool *pool;
    int origin;          /* original fd of client */
    struct tb_connection *connection;   /* */
    uint32_t transmited; /* how many bytes we are recieved or transmited */
    uint32_t expect;     /* bytes to read/write */
    size_t buf_size;
    void *buffer;        /* buffer with data */
};

struct thrift_client *make_client(struct tb_pool *pool, const int origin) {
    struct thrift_client *result;
    int flags = fcntl(origin, F_GETFL, 0);
    fcntl(origin, F_SETFL, flags | O_NONBLOCK);
    result = tb_alloc(struct thrift_client);
    result->origin = origin;
    result->pool = pool;
    result->ev = tb_alloc(struct event);
    result->buf_size = 0;
    result->transmited = 0;
    result->buffer = NULL;
    result->expect = 0;
    result->connection = NULL;
    return result;
}

void free_client(struct thrift_client *instance) {
    close(instance->origin);
    free(instance->buffer);
    event_del(instance->ev);
    free(instance->ev);
    free(instance);
}

void read_len(int fd, short event, void *arg);
void pool_connect(int fd, short event, void *arg);
void schedule_connect(struct thrift_client *tclient);

void write_data(int fd, short event, void *arg) {
    struct thrift_client *tclient = arg;
    ssize_t wrote = write(fd, tclient->buffer + tclient->transmited, 
                            tclient->buf_size - tclient->transmited);
    tb_debug("-> write_data [%d], %hd", fd, event);
    if (wrote > 0) {
        tclient->transmited += wrote;
        if (tclient->transmited == tclient->expect)
            event_set(tclient->ev, fd, EV_READ, read_len, tclient);
        else
            event_set(tclient->ev, fd, EV_WRITE, write_data, tclient);
        event_add(tclient->ev, NULL);
    } else {
        /* XXX fill it */
        tb_debug("!! wrote %d bytes, errno %d", wrote, errno);
        perror("write_data");
        if (tclient->connection != NULL) {
            dead_connection(tclient->pool, tclient->connection);
            tclient->connection = get_connection(tclient->pool);
            if (tclient->connection == NULL) {
                tb_debug("!! can't get another connect, drop client");
                free_client(tclient);
            } else {
                tb_debug("ii get another connection from pool");
                schedule_connect(tclient);
            }
        } else {
            tb_debug("!! error write_data to client");
            free_client(tclient);
        }
    }
    tb_debug("<- write_data [%d]", fd);
}

void schedule_connect(struct thrift_client *tclient) {
    struct timeval *to = NULL;
    if (tclient->connection->stat == CONN_CONNECTED) {
        tb_debug(":: schedule_connect %d to write_data",
                tclient->connection->sock);
        event_set(tclient->ev, tclient->connection->sock, 
                EV_WRITE, write_data, tclient);
        to = &tclient->connection->w_to;
    } else {
        tb_debug(":: schedule_connect %d to pool_data",
                tclient->connection->sock);
        event_set(tclient->ev, tclient->connection->sock, 
                EV_WRITE, pool_connect, tclient);
        to = &tclient->connection->w_to;
    }
    event_add(tclient->ev, to);
}

void pool_connect(int fd, short event, void *arg) {
    struct thrift_client *tclient = arg;
    int ret;
    socklen_t rlen = sizeof(ret);
    tb_debug("-> pool_connect [%d]", fd);
    if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &ret, &rlen) != -1) {
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
    dead_connection(tclient->pool, tclient->connection);
    /* get rid of a copy-paste */
    tclient->connection = get_connection(tclient->pool);
    if (tclient->connection == NULL) {
        free_client(tclient);
        tb_debug("<- pool_connect: can't get new connection from pool");
        return;
    }
    schedule_connect(tclient);
    tb_debug("<- pool_connect [%d]", fd);
}

void read_data(int fd, short event, void *arg) {
    struct thrift_client *tclient = arg;
    ssize_t got = read(fd, tclient->buffer + tclient->transmited, 
                           tclient->buf_size - tclient->transmited);
    tb_debug("-> read_data [%d]", fd);
    if (got > 0) {
        tclient->transmited += got;
        if (tclient->transmited == tclient->expect) {
            tb_debug("   data ok");
            tclient->transmited = 0;
            if (tclient->connection) {
                tb_debug("   switch to origin");
                add_connection(tclient->pool, tclient->connection);
                tclient->connection = NULL;
                event_set(tclient->ev, tclient->origin, 
                        EV_WRITE, write_data, tclient);
            } else {
                tclient->connection = get_connection(tclient->pool);
                if (tclient->connection == NULL) {
                    tb_debug("-> read_data: no free servers, delete client");
                    free_client(tclient);
                    return;
                }
                tb_debug("   switch to pooled %d [stat %d]", 
                    tclient->connection->sock, tclient->connection->stat);
                schedule_connect(tclient);
                tb_debug("<- read_data [%d]", fd);
                return;
            }
        } else {
            event_set(tclient->ev, fd, EV_READ, read_data, tclient);
        }
        event_add(tclient->ev, NULL);
    } else {
        /* XXX fill it */
        perror("read_data");
        free_client(tclient);
    }
    tb_debug("<- read_data [%d]", fd);
}

void read_len(int fd, short event, void *arg) {
    struct thrift_client *tclient = arg;
    ssize_t got;
    uint32_t len;
    tb_debug("-> read_len [%d]", fd);
    got = read(fd, &len, sizeof(len));
    if (got == 4) {
        int buf_size = ntohl(len) + sizeof(len);
        if (buf_size > tclient->buf_size) {
            void *buf = malloc(buf_size);
            if (buf == NULL) {
                tb_error("can't allocate %d bytes\n", buf_size);
                tb_debug("!! close client");
                free_client(tclient);
                tb_debug("<- read_len [%d]", fd);
                return;
            }
            free(tclient->buffer);
            tclient->buf_size = buf_size;
            tclient->buffer = buf;
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
            tb_debug("!! pool connection %d closed", fd);
        }
        if (tclient->connection != NULL) {
            dead_connection(tclient->pool, tclient->connection);
            tclient->connection = get_connection(tclient->pool);
            if (tclient->connection != NULL) {
                schedule_connect(tclient);
                return;
            }
        }
        tb_debug("!! close client");
        free_client(tclient);
    }
    tb_debug("<- read_len [%d]", fd);
}

struct event_pair {
    struct tb_pool *pool;
    struct event ev;
};

/* the maximum IPv6 address text representation is 39 bytes (???)
   and one extra byte for zero*/
#define TEXT_ADDR_LEN 40

static int addr_dump(const struct sockaddr *addr, char *buf, unsigned len) {
    switch (addr->sa_family) {
      case AF_INET:
        return inet_ntop(AF_INET, &((struct sockaddr_in *) addr)->sin_addr,
                buf, len) != NULL;
      case AF_INET6:
        return inet_ntop(AF_INET6, &((struct sockaddr_in6 *)addr)->sin6_addr,
                buf, len) != NULL;
      default:
        return 0;
    }
}

void accept_client(int fd, short event, void *arg) {
    struct event_pair *pair = (struct event_pair *)arg;
    int client;
    struct sockaddr addr;
    socklen_t len = sizeof(addr);
    /* reshedule me */
    event_add(&pair->ev, NULL);
    tb_debug("-> accept [%d]", fd);
    client = accept(fd, &addr, &len);
    if (client != -1) {
        char buffer[TEXT_ADDR_LEN];
        struct thrift_client *tclient = make_client(pair->pool, client);

        if (addr_dump(&addr, buffer, TEXT_ADDR_LEN)) {
            tb_info("get connection %s", buffer);
        }
        tb_debug("   new client %d", client);
        event_set(tclient->ev, client, EV_READ, read_len, tclient);
        event_add(tclient->ev, NULL);
    } else {
        perror("accept");
    }
    tb_debug("<- accept [%d]", fd);
}

struct tb_pool *opts_pool = NULL;
short opts_port = 9090;
extern FILE *yyin;
extern int yyparse(void);

int main(int argc, char **argv) {
    struct sockaddr_in listen_on;
    struct event_pair epair;
    int ls = socket(PF_INET, SOCK_STREAM, 0);
    int one = 1;
    if (argc != 2) {
        fprintf(stderr, "usage: %s <config-file>\n", argv[0]);
        return 1;
    }
    if (ls == -1) {
        perror("socket");
        return 1;
    }
    yyin = fopen(argv[1], "r");
    event_init();
    if (yyparse() != 0) {
        fprintf(stderr, "can't parse config file %s\n", argv[1]);
        return 1;
    }
    fclose(yyin);
    if (opts_pool == NULL) {
        fprintf(stderr, "can't find any backends\n");
        return 1;
    }
    epair.pool = opts_pool;
    listen_on.sin_port = htons(opts_port);
    if (inet_aton("0.0.0.0", &listen_on.sin_addr) == -1) {
        perror("bind");
        return 1;
    }
    setsockopt(ls, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
    if (bind(ls, (struct sockaddr *)&listen_on, sizeof(listen_on)) == -1) {
        perror("bind");
        return 1;
    }

    if (listen(ls, 4) == -1) {
        perror("listen");
        return 1;
    }
    event_set(&epair.ev, ls, EV_READ, accept_client, &epair);
    event_add(&epair.ev, NULL);
    event_dispatch();
    return 0;
}
