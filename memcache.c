#include <stdio.h>
#include <stdint.h>
#include <assert.h>
#include <string.h>
#include <arpa/inet.h>
#include <openssl/sha.h>
#include "memcache.h"
#define MAX_KEY_SIZE 41

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

int get_cache(struct tb_bucket *bucket,
        unsigned char *request, size_t qlen);
{
    return 1;
}

int store_cache(struct tb_bucket *bucket,
        unsigned char *request, size_t qlen,
        const unsigned char *response, size_t rlen)
{
    uint32_t seq_id;
    size_t at, len, klen, wrote;
    unsigned char key[41];
    if (key_hash_data(request, qlen, key, 41) == -1)
    {
        /* me bad, i must to check it out */
        bucket->transmited = bucket->buf_size;
        return 0;
    }
    {
        /**
         * We must null seq-id before calculate key.
         */
        uint32_t mlen;
        /* calculate seqid offset*/
        memcpy(&mlen, request + 4, 4);
        mlen = ntohl(mlen);
        /* frame_size + name size + name */
        at = 4 + 4 + mlen;
        memcpy(&seq_id, request + at, 4);
        memset(request + at, '\0', 4);
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
        free(bucket->buffer);
        bucket->buffer = (unsigned char *)malloc(len);
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
