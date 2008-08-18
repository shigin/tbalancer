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

int store_cache(const struct tb_bucket *bucket,
        unsigned char *request, size_t qlen,
        const unsigned char *response, size_t rlen)
{
    uint32_t seq_id, mlen;
    size_t at;
    /* calculate seqid offset*/
    memcpy(&mlen, request + 4, 4);
    mlen = ntohl(mlen);
    /* frame_size + name size + name */
    at = 4 + 4 + mlen;
    memcpy(&seq_id, request + at, 4);
    memset(request + at, '\0', 4);
    /* store request and response */

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
