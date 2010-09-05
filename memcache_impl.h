#ifndef _MEMCACHE_IMPL_H
#define _MEMCACHE_IMPL_H
#include <event.h>

#define MAX_KEY_SIZE 41

/* target for memcache routines */
#define TB_TARGET_GET 1
#define TB_TARGET_STORE 2
#define TB_TARGET_READY 3
#define TB_TARGET_ERROR 4

/* event--based store--get */
/* connect -> get -> expect_key -> read_buffer */
/* connect -> store -> expect_ok */
void memcache_connect(int fd, short event, void *arg);
void memcache_write_buffer(int fd, short event, void *arg);
void memcache_read_buffer(int fd, short event, void *arg);
void memcache_expect_key(int fd, short event, void *arg);
void memcache_expect_result(int fd, short event, void *arg);

/**
 * The routine returns hex representaion of a data.
 *
 * If the hex respresintation is longer than MAX_KEY_SIZE (try to reduce
 * network usage) routine returns SHA1 hash instead.
 *
 * Returns a pointer to new allocated data or NULL.
 */
unsigned char* key_hash(const unsigned char *data, size_t n);

/**
 * Like key_hash, but store result to buffer. 
 *
 * Returns len of writen bytes.
 * Returns -1 if buf_size is not enough to store data.
 */
int key_hash_data(const unsigned char *data, size_t n,
        unsigned char *buffer, size_t buf_size);

struct tb_bucket {
    int sock;
    int target;
    struct event *ev;
    unsigned char *buffer;
    size_t buf_size, transmited, to_send;
};

/**
 * Routine makes a query to buffer.
 */
/* XXX make request const */
int get_cache(struct tb_bucket *bucket,
        unsigned char *request, const size_t qlen);

/**
 * Routine makes a query to buffer.
 *
 * Routine can store only framed strict binary protocol of thrift.
 */
/* XXX make request const */
int store_cache(struct tb_bucket *bucket,
        unsigned char *request, const size_t qlen,
        const unsigned char *response, const size_t rlen);
#endif
