#include "types.h"
#include "constants.h"
#include "stubs.h"

static ngx_int_t
ngx_http_complex_value(ngx_http_request_t *r,
    ngx_http_complex_value_t *val, ngx_str_t *value)
{
    (void)r;
    *value = val->value;
    return NGX_OK;
}
