#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include <arpa/inet.h>
#include <openssl/sha.h>
#include "memcache.h"
#include "memcache_impl.h"
#include "common.h"

void store(unsigned char *request, const size_t qlen,
        const unsigned char *response, const size_t rlen)
{
    struct tb_bucket *bucket = tb_alloc(struct tb_bucket);
    memset(bucket, 0, sizeof(struct tb_bucket));
    bucket->ev = tb_alloc(struct event);
    store_cache(bucket, read, qlen, response, rlen);
    bucket->target = TB_TARGET_STORE;
    /* set up socket */
    event_set(bucket->ev, bucket->sock, 
        EV_WRITE, memcache_connect, bucket);
    event_add(bucket->ev, NULL);
}

void memcache_connect(int fd, short event, void *arg)
{
    struct tb_bucket *bucket = (struct tb_bucket *)arg;
    tb_debug("-> memcache_connect");
    event_set(bucket->ev, bucket->sock, EV_WRITE, 
        memcache_write_buffer, bucket);
    event_add(bucket->ev, NULL);
    tb_debug("<- memcache_connect");
}

void memcache_read_buffer(int fd, short event, void *arg)
{
    ssize_t read;
    struct tb_bucket *bucket = (struct tb_bucket *)arg;
    tb_debug("-> memcache_read_key");
    read = recv(bucket->sock, bucket->buffer, bucket->buf_size, MSG_PEEK);
    bucket->transmited += read;
    if (bucket->to_send == bucket->transmited)
    {
        bucket->target = TB_TARGET_READY;
    }
    tb_debug("<- memcache_read_key");
}

void memcache_write_buffer(int fd, short event, void *arg)
{
    size_t sent;
    struct tb_bucket *bucket = (struct tb_bucket *)arg;
    tb_debug("-> memcache_write_buffer");
    sent = send(bucket->sock, bucket->buffer + bucket->transmited, 
        bucket->to_send - bucket->transmited, 0);
    bucket->transmited += sent;
    if (bucket->to_send == bucket->transmited)
    {
        if (bucket->target == TB_TARGET_GET)
        {
            event_set(bucket->ev, bucket->sock, EV_READ,
                    memcache_expect_key, bucket);
        } else {
            if (bucket->target == TB_TARGET_STORE) {
                event_set(bucket->ev, bucket->sock, EV_READ,
                        memcache_expect_result, bucket);
            } else {
                tb_debug("!! unknown target, exit");
                tb_debug("<- memcache_write_buffer");
                bucket->target = TB_TARGET_ERROR;
                return;
            }
        }
    } else {
        event_set(bucket->ev, bucket->sock, EV_WRITE,
            memcache_write_buffer, bucket);
    }

    event_add(bucket->ev, NULL);
    tb_debug("<- memcache_write_buffer");
}

void memcache_expect_key(int fd, short event, void *arg)
{
    /* XXX response string must contains in one tcp packet */
    ssize_t read;
    static char *end_indicator = "END\r\n";
    struct tb_bucket *bucket = (struct tb_bucket *)arg;
    tb_debug("-> memcache_expect_key");
    read = recv(bucket->sock, bucket->buffer, bucket->buf_size, MSG_PEEK);
    if (read < 0)
    {
        /* XXX we need to indicate it */
        tb_debug("shit happens");
    } else {
        /* check if key exists */
        if (read < 5)
        {
            /* XXX shit happens */
            bucket->target = TB_TARGET_ERROR;
            tb_debug("<- memcache_expect_key");
            return;
        }
        if (memcmp(end_indicator, bucket->buffer, 5) == 0) {
            /* the key doesn't exist */
            bucket->target = TB_TARGET_ERROR;
            tb_debug("<- memcache_expect_key");
            return;
        } else {
            size_t pos;
            long int bytes = -1;
            unsigned int spaces = 3;
            /* get bytes */
            /* VALUE <key> <flags> <bytes> [<cas unique>]\r\n
             * <data block>\r\n */
            for (pos = 0; pos != read; ++pos)
            {
                if (bucket->buffer[pos] == '\n')
                {
                    break;
                }
                if (bucket->buffer[pos] == ' ')
                {
                    if (spaces == 0)
                    {
                        char *endptr;
                        /* XXX i hate the difference between char and 
                               unsigned char */
                        bytes = strtol((char *)bucket->buffer, &endptr, 10);
                        if (*endptr == bucket->buffer[pos])
                        {
                            tb_debug("!! can't parse bytes");
                            bucket->target = TB_TARGET_ERROR;
                            tb_debug("<- memcache_expect_key");
                            return;
                        }
                    }
                }
            } /* end for */
            /* if bytes > 0 skip the header*/
            if (bytes == -1)
            {
                tb_debug("!! can't read byte count");
            }
            read = recv(bucket->sock, bucket->buffer, pos, 0);
            assert(read == pos);
            /* agrh... to_recieve */
            bucket->to_send = bytes;
            bucket->transmited = 0;
            event_set(bucket->ev, bucket->sock, EV_READ,
                memcache_read_buffer, bucket);
            event_add(bucket->ev, NULL);
        }
    }
    tb_debug("<- memcache_expect_key");
}

void memcache_expect_result(int fd, short event, void *arg)
{
    ssize_t read;
    struct tb_bucket *bucket = (struct tb_bucket *)arg;
    tb_debug("-> memcache_expect_result");
    /* XXX fill it */
    read = recv(bucket->sock, bucket->buffer, bucket->buf_size, MSG_PEEK);
    tb_debug("<- memcache_expect_result");
}

/**
 * Make a hex representaion of data WITH terminated null.
 *
 * WARNING! The routine never check if result len is enough.
 */
static unsigned char *shex(const unsigned char *data, size_t n, unsigned char *result)
{
    size_t idx;
    for (idx=0; idx < n; ++idx)
    {
        uint8_t digit = data[idx] / 16;
        result[idx*2] = digit < 10 ? digit + '0' : digit + 'a' - 10;
        digit = data[idx] & 0x0f;
        result[idx*2 + 1] = digit < 10 ? digit + '0' : digit + 'a' - 10;
    }
    result[n*2 + 1] = '\0';
    return result;
}

unsigned char* key_hash(const unsigned char *data, size_t n)
{
    unsigned char *result;
    if (2*n + 1 > MAX_KEY_SIZE)
    {
        result = (unsigned char *)malloc(41*sizeof(char));
        key_hash_data(data, n, result, 41);
        return result;
    } else {
        result = (unsigned char *)malloc(2*n + 1);
        shex(data, n, result);
    }
    return result;
}

static void null_seq_id(unsigned char *request, size_t len, 
    uint32_t *seq_id, size_t *at)
{
    /**
     * We must null seq-id before calculate key.
     */
    uint32_t mlen;
    /* calculate seqid offset*/
    memcpy(&mlen, request + 4, 4);
    mlen = ntohl(mlen);
    /* frame_size + name size + name */
    *at = 4 + 4 + mlen;
    /* XXX we should check if at < len, assert is bad decision  */
    assert(*at < len);
    memcpy(&seq_id, request + *at, 4);
    memset(request + *at, '\0', 4);
}
 
int get_cache(struct tb_bucket *bucket,
        unsigned char *request, const size_t qlen)
{
    uint32_t seq_id;
    size_t at, len, wrote;
    unsigned char key[41];
    /* XXX we same fragment of code in store_cache */
    null_seq_id(request, qlen, &seq_id, &at);
    if (key_hash_data(request, qlen, key, 41) == -1)
    {
        /* me bad, i must to check it out */
        bucket->transmited = bucket->buf_size;
        /* restore old seq-id */
        memcpy(request + at, &seq_id, 4);
        return 0;
    }

    /* request string is get <key>*\r\n, so we need
     * 4 (get ) + 40 (key) + 3 (\r\n\0)
     */
    len = 4 + 40 + 3;
    if (len > bucket->buf_size)
    {
        /* XXX copy-paste */
        bucket->buffer = realloc(bucket->buffer, len);
        if (bucket->buffer == 0)
        {
            /* me bad, i must to check it out */
            bucket->transmited = bucket->buf_size;
            /* restore old seq-id */
            memcpy(request + at, &seq_id, 4);
            return 0;
        }
        bucket->buf_size = len;
    }

    wrote = snprintf((char *)bucket->buffer, len, "get %s\r\n", key);

    memcpy(request + at, &seq_id, 4);
    return 1;
}

int store_cache(struct tb_bucket *bucket,
        unsigned char *request, const size_t qlen,
        const unsigned char *response, const size_t rlen)
{
    uint32_t seq_id;
    size_t at, len, klen, wrote;
    unsigned char key[41];
    null_seq_id(request, len, &seq_id, &at);
    if (key_hash_data(request, qlen, key, 41) == -1)
    {
        /* me bad, i must to check it out */
        bucket->transmited = bucket->buf_size;
        /* restore old seq-id */
        memcpy(request + at, &seq_id, 4);
        return 0;
    }
    /* store request and response */
    /* command length is 4 (set) + 41 (key) + 2 (exptime) + 
       2 (flags) + 11 (bytes) + \r\n + data + \0 

       i can't imagine value which large than 4G, len(4**32) = 10

       quote from memcached/doc/protocol.txt
       <command name> <key> <flags> <exptime> <bytes> [noreply]\r\n
    */
    klen = 4 + 41 + 2 + 2 + 11 + 2;
    len = klen + rlen;
    if (len > bucket->buf_size)
    {
        /* XXX copy-paste */
        bucket->buffer = realloc(bucket->buffer, len);
        if (bucket->buffer == 0)
        {
            /* me bad, i must to check it out */
            bucket->transmited = bucket->buf_size;
            /* restore old seq-id */
            memcpy(request + at, &seq_id, 4);
            return 0;
        }
        bucket->buf_size = len;
    }
    wrote = snprintf((char *)bucket->buffer, klen, 
        "set %s 0 0 %d\r\n", key, rlen);

    /* XXX check if writev can help me */
    memcpy(bucket->buffer + wrote + 1, response, rlen);
    bucket->to_send = wrote + rlen;
    bucket->transmited = 0;
    /* restore old seq-id */
    memcpy(request + at, &seq_id, 4);
    return 1;
}

int key_hash_data(const unsigned char *data, size_t n,
        unsigned char *buffer, size_t buf_size)
{
    if (2*n + 1 > MAX_KEY_SIZE)
    {
        unsigned char tmp[20];
        assert (buf_size >= 41);
        SHA1(data, n, tmp);
        printf("first byte is %d\n", tmp[0]);
        shex(tmp, 20, buffer);
        return 41;
    } else {
        assert (buf_size >= 2*n + 1);
        shex(data, n, buffer);
        return 2*n + 1;
    }
    return -1;
}

int main(void)
{
    unsigned char x[44];
    printf("?123 is %s\n", shex((unsigned char *)"\xff 123a", 5, x));
    printf("123 is %s\n", key_hash((unsigned char *)"123a", 4));
    return 0;
}
