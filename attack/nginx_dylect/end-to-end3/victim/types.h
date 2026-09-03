
#ifndef _NGX_TYPES_H_INCLUDED_
#define _NGX_TYPES_H_INCLUDED_

#include "config.h"
#include "constants.h"

typedef struct {
    size_t      len;
    u_char     *data;
} ngx_str_t;

/* --- Scalar typedefs --- */

typedef uintptr_t               ngx_atomic_uint_t;
typedef int                     ngx_socket_t;
typedef uintptr_t               ngx_msec_t;
typedef void                   *ngx_buf_tag_t;
typedef struct stat             ngx_file_info_t;

/* --- Opaque forward declarations (pointer-only usage) --- */

typedef struct ngx_open_file_s          ngx_open_file_t;
typedef struct ngx_event_s              ngx_event_t;
typedef struct ngx_listening_s          ngx_listening_t;
typedef struct ngx_proxy_protocol_s     ngx_proxy_protocol_t;
typedef struct ngx_udp_connection_s     ngx_udp_connection_t;
typedef struct ngx_http_upstream_s      ngx_http_upstream_t;
typedef struct ngx_array_s              ngx_array_t;
typedef struct ngx_http_request_body_s  ngx_http_request_body_t;
typedef struct ngx_http_postponed_request_s   ngx_http_postponed_request_t;
typedef struct ngx_http_post_subrequest_s     ngx_http_post_subrequest_t;
typedef struct ngx_http_posted_request_s      ngx_http_posted_request_t;
typedef struct ngx_http_variable_value_s      ngx_http_variable_value_t;
typedef struct ngx_http_connection_s          ngx_http_connection_t;
typedef struct ngx_http_v2_stream_s           ngx_http_v2_stream_t;
typedef struct ngx_http_v3_parse_s            ngx_http_v3_parse_t;
typedef struct ngx_http_cleanup_s             ngx_http_cleanup_t;

/* --- Function pointer typedefs (unused, void* compatible) --- */

typedef void   *ngx_log_handler_pt;
typedef void   *ngx_log_writer_pt;
typedef void   *ngx_recv_pt;
typedef void   *ngx_send_pt;
typedef void   *ngx_recv_chain_pt;
typedef void   *ngx_send_chain_pt;
typedef void   *ngx_http_event_handler_pt;
typedef void   *ngx_http_handler_pt;
typedef void   *ngx_http_log_handler_pt;

/* ABI compat macros (no-op) */
#define NGX_COMPAT_BEGIN(n)
#define NGX_COMPAT_END

/* --- Composite types needed before struct definitions --- */

typedef struct ngx_queue_s  ngx_queue_t;
struct ngx_queue_s {
    ngx_queue_t  *prev;
    ngx_queue_t  *next;
};

typedef struct {
    ngx_uint_t    hash;
    ngx_str_t     key;
    ngx_str_t     value;
    u_char       *lowcase_key;
} ngx_table_elt_t;

/* Minimal headers_in: only fields used by auth_basic */
typedef struct {
    ngx_table_elt_t  *authorization;
    ngx_str_t         user;
    ngx_str_t         passwd;
} ngx_http_headers_in_t;

typedef struct {
    ngx_int_t         status;
} ngx_http_headers_out_t;

/* --- Original struct definitions (kept identical) --- */

struct ngx_chain_s {
    ngx_buf_t    *buf;
    ngx_chain_t  *next;
};

struct ngx_pool_large_s {
    ngx_pool_large_t     *next;
    void                 *alloc;
};

typedef struct {
    u_char               *last;
    u_char               *end;
    ngx_pool_t           *next;
    ngx_uint_t            failed;
} ngx_pool_data_t;


struct ngx_log_s {
    ngx_uint_t           log_level;
    ngx_open_file_t     *file;
    ngx_atomic_uint_t    connection;
    time_t               disk_full_time;
    ngx_log_handler_pt   handler;
    void                *data;
    ngx_log_writer_pt    writer;
    void                *wdata;
    /*
     * we declare "action" as "char *" because the actions are usually
     * the static strings and in the "u_char *" case we have to override
     * their types all the time
     */
    char                *action;
    ngx_log_t           *next;
    NGX_COMPAT_BEGIN(5)
    NGX_COMPAT_END
};

struct ngx_pool_cleanup_s {
    ngx_pool_cleanup_pt   handler;
    void                 *data;
    ngx_pool_cleanup_t   *next;
};

struct ngx_pool_s {
    ngx_pool_data_t       d;
    size_t                max;
    ngx_pool_t           *current;
    ngx_chain_t          *chain;
    ngx_pool_large_t     *large;
    ngx_pool_cleanup_t   *cleanup;
    ngx_log_t            *log;
};

typedef struct {
    ngx_str_t                   value;
    ngx_uint_t                 *flushes;
    void                       *lengths;
    void                       *values;

    union {
        size_t                  size;
    } u;
} ngx_http_complex_value_t;

typedef struct {
    ngx_http_complex_value_t  *realm;
    ngx_http_complex_value_t  *user_file;
} ngx_http_auth_basic_loc_conf_t;

struct ngx_connection_s {
    void               *data;
    ngx_event_t        *read;
    ngx_event_t        *write;

    ngx_socket_t        fd;

    ngx_recv_pt         recv;
    ngx_send_pt         send;
    ngx_recv_chain_pt   recv_chain;
    ngx_send_chain_pt   send_chain;

    ngx_listening_t    *listening;

    off_t               sent;

    ngx_log_t          *log;

    ngx_pool_t         *pool;

    int                 type;

    struct sockaddr    *sockaddr;
    socklen_t           socklen;
    ngx_str_t           addr_text;

    ngx_proxy_protocol_t  *proxy_protocol;

    ngx_udp_connection_t  *udp;

    struct sockaddr    *local_sockaddr;
    socklen_t           local_socklen;

    ngx_buf_t          *buffer;

    ngx_queue_t         queue;

    ngx_atomic_uint_t   number;

    ngx_msec_t          start_time;
    ngx_uint_t          requests;

    unsigned            buffered:8;

    unsigned            log_error:3;     /* ngx_connection_log_error_e */

    unsigned            timedout:1;
    unsigned            error:1;
    unsigned            destroyed:1;
    unsigned            pipeline:1;

    unsigned            idle:1;
    unsigned            reusable:1;
    unsigned            close:1;
    unsigned            shared:1;

    unsigned            sendfile:1;
    unsigned            sndlowat:1;
    unsigned            tcp_nodelay:2;   /* ngx_connection_tcp_nodelay_e */
    unsigned            tcp_nopush:2;    /* ngx_connection_tcp_nopush_e */

    unsigned            need_last_buf:1;
    unsigned            need_flush_buf:1;
};

struct ngx_file_s {
    ngx_fd_t                   fd;
    ngx_str_t                  name;
    ngx_file_info_t            info;

    off_t                      offset;
    off_t                      sys_offset;

    ngx_log_t                 *log;

    unsigned                   valid_info:1;
    unsigned                   directio:1;
};

struct ngx_buf_s {
    u_char          *pos;
    u_char          *last;
    off_t            file_pos;
    off_t            file_last;

    u_char          *start;         /* start of buffer */
    u_char          *end;           /* end of buffer */
    ngx_buf_tag_t    tag;
    ngx_file_t      *file;
    ngx_buf_t       *shadow;


    /* the buf's content could be changed */
    unsigned         temporary:1;

    /*
     * the buf's content is in a memory cache or in a read only memory
     * and must not be changed
     */
    unsigned         memory:1;

    /* the buf's content is mmap()ed and must not be changed */
    unsigned         mmap:1;

    unsigned         recycled:1;
    unsigned         in_file:1;
    unsigned         flush:1;
    unsigned         sync:1;
    unsigned         last_buf:1;
    unsigned         last_in_chain:1;

    unsigned         last_shadow:1;
    unsigned         temp_file:1;

    /* STUB */ int   num;
};

struct ngx_http_request_s {
    uint32_t                          signature;         /* "HTTP" */

    ngx_connection_t                 *connection;

    void                            **ctx;
    void                            **main_conf;
    void                            **srv_conf;
    void                            **loc_conf;

    ngx_http_event_handler_pt         read_event_handler;
    ngx_http_event_handler_pt         write_event_handler;

    ngx_http_upstream_t              *upstream;
    ngx_array_t                      *upstream_states;
                                         /* of ngx_http_upstream_state_t */

    ngx_pool_t                       *pool;
    ngx_buf_t                        *header_in;

    ngx_http_headers_in_t             headers_in;
    ngx_http_headers_out_t            headers_out;

    ngx_http_request_body_t          *request_body;

    time_t                            lingering_time;
    time_t                            start_sec;
    ngx_msec_t                        start_msec;

    ngx_uint_t                        method;
    ngx_uint_t                        http_version;

    ngx_str_t                         request_line;
    ngx_str_t                         uri;
    ngx_str_t                         args;
    ngx_str_t                         exten;
    ngx_str_t                         unparsed_uri;

    ngx_str_t                         method_name;
    ngx_str_t                         http_protocol;
    ngx_str_t                         schema;

    ngx_chain_t                      *out;
    ngx_http_request_t               *main;
    ngx_http_request_t               *parent;
    ngx_http_postponed_request_t     *postponed;
    ngx_http_post_subrequest_t       *post_subrequest;
    ngx_http_posted_request_t        *posted_requests;

    ngx_int_t                         phase_handler;
    ngx_http_handler_pt               content_handler;
    ngx_uint_t                        access_code;

    ngx_http_variable_value_t        *variables;

    size_t                            limit_rate;
    size_t                            limit_rate_after;

    /* used to learn the Apache compatible response length without a header */
    size_t                            header_size;

    off_t                             request_length;

    ngx_uint_t                        err_status;

    ngx_http_connection_t            *http_connection;
    ngx_http_v2_stream_t             *stream;
    ngx_http_v3_parse_t              *v3_parse;

    ngx_http_log_handler_pt           log_handler;

    ngx_http_cleanup_t               *cleanup;

    in_port_t                         port;

    unsigned                          count:16;
    unsigned                          subrequests:8;
    unsigned                          blocked:8;

    unsigned                          aio:1;

    unsigned                          http_state:4;

    /* URI with "/." and on Win32 with "//" */
    unsigned                          complex_uri:1;

    /* URI with "%" */
    unsigned                          quoted_uri:1;

    /* URI with "+" */
    unsigned                          plus_in_uri:1;

    /* URI with empty path */
    unsigned                          empty_path_in_uri:1;

    unsigned                          invalid_header:1;

    unsigned                          add_uri_to_alias:1;
    unsigned                          valid_location:1;
    unsigned                          valid_unparsed_uri:1;
    unsigned                          uri_changed:1;
    unsigned                          uri_changes:4;

    unsigned                          request_body_in_single_buf:1;
    unsigned                          request_body_in_file_only:1;
    unsigned                          request_body_in_persistent_file:1;
    unsigned                          request_body_in_clean_file:1;
    unsigned                          request_body_file_group_access:1;
    unsigned                          request_body_file_log_level:3;
    unsigned                          request_body_no_buffering:1;

    unsigned                          subrequest_in_memory:1;
    unsigned                          waited:1;

    unsigned                          proxy:1;
    unsigned                          bypass_cache:1;
    unsigned                          no_cache:1;

    /*
     * instead of using the request context data in
     * ngx_http_limit_conn_module and ngx_http_limit_req_module
     * we use the bit fields in the request structure
     */
    unsigned                          limit_conn_status:2;
    unsigned                          limit_req_status:3;

    unsigned                          limit_rate_set:1;
    unsigned                          limit_rate_after_set:1;

    unsigned                          pipeline:1;
    unsigned                          chunked:1;
    unsigned                          header_only:1;
    unsigned                          expect_trailers:1;
    unsigned                          keepalive:1;
    unsigned                          lingering_close:1;
    unsigned                          discard_body:1;
    unsigned                          reading_body:1;
    unsigned                          internal:1;
    unsigned                          error_page:1;
    unsigned                          filter_finalize:1;
    unsigned                          post_action:1;
    unsigned                          request_complete:1;
    unsigned                          request_output:1;
    unsigned                          header_sent:1;
    unsigned                          response_sent:1;
    unsigned                          expect_tested:1;
    unsigned                          root_tested:1;
    unsigned                          done:1;
    unsigned                          logged:1;
    unsigned                          terminated:1;

    unsigned                          buffered:4;

    unsigned                          main_filter_need_in_memory:1;
    unsigned                          filter_need_in_memory:1;
    unsigned                          filter_need_temporary:1;
    unsigned                          preserve_body:1;
    unsigned                          allow_ranges:1;
    unsigned                          subrequest_ranges:1;
    unsigned                          single_range:1;
    unsigned                          disable_not_modified:1;
    unsigned                          stat_reading:1;
    unsigned                          stat_writing:1;
    unsigned                          stat_processing:1;

    unsigned                          background:1;
    unsigned                          health_check:1;

    /* used to parse HTTP headers */

    ngx_uint_t                        state;

    ngx_uint_t                        header_hash;
    ngx_uint_t                        lowcase_index;
    u_char                            lowcase_header[NGX_HTTP_LC_HEADER_LEN];

    u_char                           *header_name_start;
    u_char                           *header_name_end;
    u_char                           *header_start;
    u_char                           *header_end;

    /*
     * a memory that can be reused after parsing a request line
     * via ngx_http_ephemeral_t
     */

    u_char                           *uri_start;
    u_char                           *uri_end;
    u_char                           *uri_ext;
    u_char                           *args_start;
    u_char                           *request_start;
    u_char                           *request_end;
    u_char                           *method_end;
    u_char                           *schema_start;
    u_char                           *schema_end;
    u_char                           *host_start;
    u_char                           *host_end;

    unsigned                          http_minor:16;
    unsigned                          http_major:16;
};

#endif /* _NGX_TYPES_H_INCLUDED_ */
