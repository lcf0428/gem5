#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sys/mman.h>
#include <unistd.h>
#include <mmintrin.h>
#include <x86intrin.h>

#include "types.h"
#include "constants.h"
#include "stubs.h"
// #include "../../../gem5/gem5/m5ops.h"
// #include "gem5/m5ops.h"
#include <gem5/m5ops.h>

/* --- Pull in stub / allocator implementations (single TU) --- */
#include "stubs.c"
#include "ngx_pnalloc.c"

/* =================================================================
 *  Functions called by ngx_auth.c
 * ================================================================= */

volatile uint64_t tmp = 0;

static u_char ngx_basis64[] = {
    77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77,
    77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77,
    77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 62, 77, 77, 77, 63,
    52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 77, 77, 77, 77, 77, 77,
    77,  0,  1,  2,  3,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 14,
    15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 77, 77, 77, 77, 77,
    77, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40,
    41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, 77, 77, 77, 77, 77,
    77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77,
    77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77,
    77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77,
    77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77,
    77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77,
    77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77,
    77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77,
    77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77
};

static u_char rubbish[] = {
    0x19, 0x92, 0x01, 0x76, 00, 00, 00, 00, 
    0x80, 0xb8, 0x21, 00, 00, 00, 00, 00, 
    0x1b, 00, 00, 00, 00, 00, 00, 00,
    0x78, 00, 00, 00, 00, 00, 00, 00, 
    0x1a, 00, 00, 00, 00, 00, 00, 00, 
    0xf8, 0xb8, 0x21, 00, 00, 00, 00, 00, 
    0x1c ,00, 00, 00, 00, 00, 00, 00, 
    0x08, 00, 00, 00, 00, 00, 00, 00
};

static ngx_int_t
ngx_decode_base64(ngx_str_t *dst, ngx_str_t *src)
{
    size_t   len;
    u_char  *d, *s;

    for (len = 0; len < src->len; len++) {
        if (src->data[len] == '=') break;
        if (ngx_basis64[src->data[len]] == 77) return NGX_ERROR;
    }
    if (len % 4 == 1) return NGX_ERROR;

    s = src->data;
    d = dst->data;

    while (len > 3) {
        *d++ = (u_char) (ngx_basis64[s[0]] << 2 | ngx_basis64[s[1]] >> 4);
        *d++ = (u_char) (ngx_basis64[s[1]] << 4 | ngx_basis64[s[2]] >> 2);
        *d++ = (u_char) (ngx_basis64[s[2]] << 6 | ngx_basis64[s[3]]);
        s += 4;
        len -= 4;
    }
    if (len > 1)
        *d++ = (u_char) (ngx_basis64[s[0]] << 2 | ngx_basis64[s[1]] >> 4);
    if (len > 2)
        *d++ = (u_char) (ngx_basis64[s[1]] << 4 | ngx_basis64[s[2]] >> 2);

    dst->len = d - dst->data;
    return NGX_OK;
}

static ssize_t
ngx_read_file(ngx_file_t *file, u_char *buf, size_t size, off_t offset)
{
    ssize_t n = pread(file->fd, buf, size, offset);
    if (n == -1) return NGX_ERROR;
    file->offset += n;
    return n;
}

static u_char *
ngx_cpystrn(u_char *dst, u_char *src, size_t n)
{
    if (n == 0) return dst;
    while (--n) {
        *dst = *src;
        if (*dst == '\0') return dst;
        dst++;
        src++;
    }
    *dst = '\0';
    return dst;
}

static ngx_int_t
ngx_http_auth_basic_set_realm(ngx_http_request_t *r, ngx_str_t *realm)
{
    (void)r; (void)realm;
    return NGX_HTTP_UNAUTHORIZED;
}

/* Stub: hash the key with the salt.  For the harness we just copy the
   salt so that ngx_strcmp always runs a full comparison. */
static ngx_int_t
ngx_crypt(ngx_pool_t *pool, u_char *key, u_char *salt, u_char **encrypted)
{
    (void)key;
    size_t len = strlen((const char *) salt);
    *encrypted = ngx_pnalloc(pool, len + 1);
    if (*encrypted == NULL) return NGX_ERROR;
    memcpy(*encrypted, salt, len + 1);
    return NGX_OK;
}



static ngx_int_t
ngx_http_auth_basic_crypt_handler(ngx_http_request_t *r, ngx_str_t *passwd,
    ngx_str_t *realm)
{
    ngx_int_t   rc;
    u_char     *encrypted;

    rc = ngx_crypt(r->pool, r->headers_in.passwd.data, passwd->data,
                   &encrypted);

    if (rc != NGX_OK) {
        return NGX_HTTP_INTERNAL_SERVER_ERROR;
    }

    // printf(">>> interval len is %d\n", passwd->data - r->headers_in.passwd.data);
    // for (uint64_t i = r->headers_in.passwd.data; i < passwd->data; i++) {
    //     printf("%d ", *(uint8_t *)(i));
    // }
    // printf("\n");
    // fflush(stdout);

    // for (uint64_t i = passwd->data; i < passwd->data + 3; i++) {
    //     printf("%d ", *(uint8_t *)(i));
    // }
    // printf("end\n");

    printf("header in passwd is 0x%llx, passwd->data is 0x%llx\n", r->headers_in.passwd.data, passwd->data);

    uint8_t* start_addr = r->headers_in.passwd.data - 82;

    // printf("the snapshot: \n");
    // for (uint64_t i = start_addr + 576; i < start_addr + 640; i++) {
    //     printf("%d ", *(uint8_t *)(i));
    // }

    // printf("\nnext cacheline: \n");
    // for (uint64_t i = start_addr + 64; i < start_addr + 128; i++) {
    //     printf("%d ", *(uint8_t *)(i));
    // }
    // printf("===== end ====\n");

    // printf("start addr is 0x%llx", start_addr);

    _mm_clflush(start_addr);
     void *ptr = mmap(NULL, 4096, PROT_READ | PROT_WRITE,
                     MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    uint8_t * new_page = (uint8_t*)(ptr);
    new_page[0] = 0x01;
    _mm_clflush(new_page);

    // for (int i = 32; i < 52; i++) {
    //     memcpy(start_addr + i * 64, rubbish, 64);
    // }
    // m5_reset_stats(0, 0);
    // printf("iterate over the place for 4096 times\n");
    _mm_mfence();
    for (int i = 0; i < 8192; i++) {
        _mm_clflush(start_addr + 576);
        _mm_mfence();
        // tmp = *(volatile uint64_t*)(start_addr + 156);
        tmp = *(volatile uint64_t*)(start_addr + 578);
    }
    _mm_mfence();
    // m5_dump_stats(0, 0);

    // printf("tmp is %llx\n", tmp);

    if (ngx_strcmp(encrypted, passwd->data) == 0) {
        return NGX_OK;
    }

    return ngx_http_auth_basic_set_realm(r, realm);
}

/* --- The two target functions (identical to nginx) --- */
#include "ngx_auth.c"

/* =================================================================
 *  Harness
 * ================================================================= */

static const char b64_table[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static size_t
base64_encode(const u_char *src, size_t src_len, u_char *dst)
{
    u_char *d = dst;
    size_t  i;

    for (i = 0; i + 2 < src_len; i += 3) {
        *d++ = b64_table[(src[i] >> 2) & 0x3F];
        *d++ = b64_table[((src[i] & 0x3) << 4) | ((src[i+1] >> 4) & 0xF)];
        *d++ = b64_table[((src[i+1] & 0xF) << 2) | ((src[i+2] >> 6) & 0x3)];
        *d++ = b64_table[src[i+2] & 0x3F];
    }
    if (i < src_len) {
        *d++ = b64_table[(src[i] >> 2) & 0x3F];
        if (i + 1 < src_len) {
            *d++ = b64_table[((src[i] & 0x3) << 4) |
                             ((src[i+1] >> 4) & 0xF)];
            *d++ = b64_table[((src[i+1] & 0xF) << 2)];
        } else {
            *d++ = b64_table[((src[i] & 0x3) << 4)];
            *d++ = '=';
        }
        *d++ = '=';
    }
    return (size_t)(d - dst);
}

static ngx_pool_t *
ngx_create_pool(size_t size)
{
    u_char     *m;
    ngx_pool_t *p;

    m = (u_char *) memalign(NGX_POOL_ALIGNMENT, size);
    if (m == NULL) return NULL;

    p = (ngx_pool_t *) m;
    p->d.last   = m + sizeof(ngx_pool_t);
    p->d.end    = m + size;
    p->d.next   = NULL;
    p->d.failed = 0;

    size -= sizeof(ngx_pool_t);
    p->max = (size < NGX_MAX_ALLOC_FROM_POOL) ? size : NGX_MAX_ALLOC_FROM_POOL;

    p->current = p;
    p->chain   = NULL;
    p->large   = NULL;
    p->cleanup = NULL;
    p->log     = NULL;

    return p;
}

static inline uint64_t rdtsc(void)
{
    uint32_t lo, hi;
    __asm__ volatile ("rdtsc" : "=a"(lo), "=d"(hi));
    return ((uint64_t)hi << 32) | lo;
}

static void delay_cycles(uint64_t cycles)
{
    uint64_t start = rdtsc();

    while (rdtsc() - start < cycles) {
        __asm__ volatile ("pause");
    }
}


/*
 * Usage: ./victim <user:password> <hash_string>
 *
 * Writes /tmp/htpasswd_test with  user:<hash>  (NO trailing newline)
 * so the handler takes the post-loop ngx_pnalloc code path.
 */
int main(int argc, char **argv)
{
    delay_cycles(5000000ULL);
    // printf("enter the main function 3000000\n");
    const char *credentials  = argv[1];   /* "user:password" */
    const char *hash_string  = argv[2];   /* hash in htpasswd */
    (void)argc;

    size_t username_len = (size_t)(strchr(credentials, ':') - credentials);

    /* Write htpasswd — NO trailing newline */
    // FILE *fp = fopen("/tmp/htpasswd_test", "w");
    // fwrite(credentials, 1, username_len, fp);
    // fputc(':', fp);
    // fwrite(hash_string, 1, strlen(hash_string), fp);
    // fclose(fp);

    int fd = open("/tmp/htpasswd_test", O_WRONLY | O_CREAT | O_TRUNC, 0644);
    write(fd, credentials, username_len);
    write(fd, ":", 1);
    write(fd, hash_string, strlen(hash_string));
    close(fd);

    for (int i = 0; i < 1; i++) {
        /* Base64-encode credentials → "Basic <encoded>" */
        size_t cred_len = strlen(credentials);
        size_t enc_max  = ((cred_len + 2) / 3) * 4;
        u_char *auth_value = (u_char *) malloc(6 + enc_max + 1);
        memcpy(auth_value, "Basic ", 6);
        size_t enc_len = base64_encode((u_char *)credentials, cred_len,
                                    auth_value + 6);
        auth_value[6 + enc_len] = '\0';

        /* Set up pool, log, connection */
        ngx_pool_t       *pool = ngx_create_pool(4096);
        ngx_log_t         log;
        ngx_connection_t  conn;
        memset(&log,  0, sizeof(log));
        memset(&conn, 0, sizeof(conn));
        conn.log = &log;

        /* Authorization header */
        ngx_table_elt_t auth_elt;
        memset(&auth_elt, 0, sizeof(auth_elt));
        auth_elt.value.len  = 6 + enc_len;
        auth_elt.value.data = auth_value;

        /* Request */
        ngx_http_request_t r;
        memset(&r, 0, sizeof(r));
        r.pool       = pool;
        r.connection = &conn;
        r.headers_in.authorization = &auth_elt;

        /* Point stub htpasswd path */
        stub_user_file_cv.value.data = (u_char *) "/tmp/htpasswd_test";
        stub_user_file_cv.value.len  = 18;


        ngx_int_t res = ngx_http_auth_basic_handler(&r);

        printf("the res is %d\n", res);

        free(auth_value);
    }

    unlink("/tmp/htpasswd_test");

    printf("finally! finished\n");
    fflush(stdout);
    m5_exit(1000);
    return 0;
}
