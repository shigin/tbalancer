#ifndef MEMCACHE_H__
#define MEMCACHE_H__
#include <stdlib.h>

/** Routine stores request and response to memcached.
 *
 * The saving is performed by libevent, i.e. the key may not be stored
 * on function return.
 */
void store(unsigned char *request, const size_t qlen,
        const unsigned char *response, const size_t rlen);

int get(unsigned char *request, const size_t qlen,
        unsigned char *response, size_t rlen);

#endif
