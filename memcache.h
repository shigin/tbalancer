#ifndef MEMCACHE_H__
#define MEMCACHE_H__
#include <stdlib.h>
/**
 * The routine returns hex representaion of a data.
 *
 * If the hex respresintation is longer than MAX_KEY_SIZE (try to reduce network usage)
 * routine returns SHA1 hash instead.
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

struct tb_bucket
{
};

/**
 * Routine can store only framed strict binary protocol of thrift.
 */
int store_cache(const struct tb_bucket *bucket,
        unsigned char *request, size_t qlen,
        const unsigned char *response, size_t rlen);
#endif
