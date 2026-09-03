#ifndef _NGX_STUBS_H_INCLUDED_
#define _NGX_STUBS_H_INCLUDED_

#include "types.h"

static ngx_http_complex_value_t stub_realm_cv = {
    .value = { .len = 10, .data = (u_char *) "Restricted" }
};

static ngx_http_complex_value_t stub_user_file_cv = {
    .value = { .len = 13, .data = (u_char *) "/tmp/htpasswd" }
};

static ngx_http_auth_basic_loc_conf_t stub_alcf = {
    .realm     = &stub_realm_cv,
    .user_file = &stub_user_file_cv
};

// re-define macro to return our stub
#define ngx_http_get_module_loc_conf(r, module)  (&stub_alcf)

static ngx_int_t
ngx_http_complex_value(ngx_http_request_t *r,
    ngx_http_complex_value_t *val, ngx_str_t *value);

#endif /* _NGX_STUBS_H_INCLUDED_ */
