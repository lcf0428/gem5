#ifndef _NGX_CONFIG_H_INCLUDED_
#define _NGX_CONFIG_H_INCLUDED_

#include <sys/types.h>
#include <unistd.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <strings.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <malloc.h>             /* memalign() */

#ifndef ngx_inline
#define ngx_inline      inline
#endif

#define LF     (u_char) '\n'
#define CR     (u_char) '\r'
#define CRLF   "\r\n"

#define ngx_strcmp(s1, s2)      strcmp((const char *) s1, (const char *) s2)
#define ngx_strncmp(s1, s2, n)  strncmp((const char *) s1, (const char *) s2, n)
#define ngx_strncasecmp(s1, s2, n)  strncasecmp((const char *) s1, (const char *) s2, n)

#define ngx_memzero(buf, n)       (void) memset(buf, 0, n)
#define ngx_memmove(dst, src, n)  (void) memmove(dst, src, n)
#define ngx_explicit_memzero(buf, n) (void) memset(buf, 0, n)

#define ngx_str_set(str, text)                                               \
    (str)->len = sizeof(text) - 1; (str)->data = (u_char *) text

#define ngx_base64_decoded_length(len)  (((len + 3) / 4) * 3)

/* File operations */
#define ngx_open_file(name, mode, create, access)                            \
    open((const char *) name, mode|create, access)
#define ngx_close_file               close
#define ngx_open_file_n              "open()"
#define ngx_read_file_n              "pread()"
#define ngx_close_file_n             "close()"
#define ngx_errno                    errno

/* Logging (no-op stub) */
#define ngx_log_error(level, log, ...)  (void)0

/* Pool alignment */
#define NGX_ALIGNMENT           sizeof(unsigned long)
// #define NGX_POOL_ALIGNMENT      16
#define NGX_POOL_ALIGNMENT 4096
#define NGX_MAX_ALLOC_FROM_POOL 4095
#define ngx_align_ptr(p, a)                                                  \
    (u_char *) (((uintptr_t) (p) + ((uintptr_t) a - 1)) & ~((uintptr_t) a - 1))

typedef void (*ngx_pool_cleanup_pt)(void *data);

typedef intptr_t        ngx_int_t;
typedef uintptr_t       ngx_uint_t;
typedef intptr_t        ngx_flag_t;

typedef int             ngx_fd_t;
typedef int             ngx_err_t;

typedef struct ngx_http_request_s     ngx_http_request_t;
typedef struct ngx_connection_s       ngx_connection_t;
typedef struct ngx_pool_s             ngx_pool_t;
typedef struct ngx_chain_s            ngx_chain_t;
typedef struct ngx_pool_large_s       ngx_pool_large_t;
typedef struct ngx_pool_cleanup_s     ngx_pool_cleanup_t;
typedef struct ngx_buf_s              ngx_buf_t;
typedef struct ngx_file_s             ngx_file_t;
typedef struct ngx_log_s              ngx_log_t;

#endif /* _NGX_CONFIG_H_INCLUDED_ */
