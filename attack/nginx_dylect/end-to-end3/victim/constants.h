#ifndef _NGX_CONSTANTS_H_INCLUDED_
#define _NGX_CONSTANTS_H_INCLUDED_

#define NGX_HTTP_AUTH_BUF_SIZE  2048

#define  NGX_OK          0
#define  NGX_ERROR      -1
#define  NGX_DECLINED   -5

#define NGX_LOG_ERR               0
#define NGX_LOG_ALERT             1
#define NGX_LOG_CRIT              3
#define NGX_LOG_INFO              7

#define NGX_HTTP_UNAUTHORIZED          401
#define NGX_HTTP_FORBIDDEN             403
#define NGX_HTTP_INTERNAL_SERVER_ERROR  500

#define NGX_FILE_RDONLY          O_RDONLY
#define NGX_FILE_OPEN            0
#define NGX_INVALID_FILE         (-1)
#define NGX_FILE_ERROR           (-1)
#define NGX_ENOENT               ENOENT

#define NGX_HTTP_LC_HEADER_LEN   32

#endif /* _NGX_CONSTANTS_H_INCLUDED_ */
