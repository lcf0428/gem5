#include "types.h"
#include "constants.h"
#include "stubs.h"

ngx_int_t
ngx_http_auth_basic_user(ngx_http_request_t *r)
{
    ngx_str_t   auth, encoded;
    ngx_uint_t  len;

    if (r->headers_in.user.len == 0 && r->headers_in.user.data != NULL) {
        return NGX_DECLINED;
    }

    if (r->headers_in.authorization == NULL) {
        r->headers_in.user.data = (u_char *) "";
        return NGX_DECLINED;
    }

    encoded = r->headers_in.authorization->value;

    if (encoded.len < sizeof("Basic ") - 1
        || ngx_strncasecmp(encoded.data, (u_char *) "Basic ",
                           sizeof("Basic ") - 1)
           != 0)
    {
        r->headers_in.user.data = (u_char *) "";
        return NGX_DECLINED;
    }

    encoded.len -= sizeof("Basic ") - 1;
    encoded.data += sizeof("Basic ") - 1;

    while (encoded.len && encoded.data[0] == ' ') {
        encoded.len--;
        encoded.data++;
    }

    if (encoded.len == 0) {
        r->headers_in.user.data = (u_char *) "";
        return NGX_DECLINED;
    }

    auth.len = ngx_base64_decoded_length(encoded.len);
    auth.data = ngx_pnalloc(r->pool, auth.len + 1);
    if (auth.data == NULL) {
        return NGX_ERROR;
    }

    if (ngx_decode_base64(&auth, &encoded) != NGX_OK) {
        r->headers_in.user.data = (u_char *) "";
        return NGX_DECLINED;
    }

    auth.data[auth.len] = '\0';

    for (len = 0; len < auth.len; len++) {
        if (auth.data[len] == ':') {
            break;
        }
    }

    if (len == 0 || len == auth.len) {
        r->headers_in.user.data = (u_char *) "";
        return NGX_DECLINED;
    }

    r->headers_in.user.len = len;
    r->headers_in.user.data = auth.data;
    r->headers_in.passwd.len = auth.len - len - 1;
    r->headers_in.passwd.data = &auth.data[len + 1];

    return NGX_OK;
}


static ngx_int_t
ngx_http_auth_basic_handler(ngx_http_request_t *r)
{
    off_t                            offset;
    ssize_t                          n;
    ngx_fd_t                         fd;
    ngx_int_t                        rc;
    ngx_err_t                        err;
    ngx_str_t                        pwd, realm, user_file;
    ngx_uint_t                       i, level, login, left, passwd;
    ngx_file_t                       file;
    ngx_http_auth_basic_loc_conf_t  *alcf;
    u_char                           buf[NGX_HTTP_AUTH_BUF_SIZE];
    enum {
        sw_login,
        sw_passwd,
        sw_skip
    } state;

    //replace with stub
    alcf = ngx_http_get_module_loc_conf(r, ngx_http_auth_basic_module);

    if (alcf->realm == NULL || alcf->user_file == NULL) {
        return NGX_DECLINED;
    }

    if (ngx_http_complex_value(r, alcf->realm, &realm) != NGX_OK) {
        return NGX_ERROR;
    }

    if (realm.len == 3 && ngx_strncmp(realm.data, "off", 3) == 0) {
        return NGX_DECLINED;
    }

    rc = ngx_http_auth_basic_user(r);

    if (rc == NGX_DECLINED) {

        ngx_log_error(NGX_LOG_INFO, r->connection->log, 0,
                      "no user/password was provided for basic authentication");

        return ngx_http_auth_basic_set_realm(r, &realm);
    }

    if (rc == NGX_ERROR) {
        return NGX_HTTP_INTERNAL_SERVER_ERROR;
    }

    if (ngx_http_complex_value(r, alcf->user_file, &user_file) != NGX_OK) {
        return NGX_ERROR;
    }

    fd = ngx_open_file(user_file.data, NGX_FILE_RDONLY, NGX_FILE_OPEN, 0);

    if (fd == NGX_INVALID_FILE) {
        err = ngx_errno;

        if (err == NGX_ENOENT) {
            level = NGX_LOG_ERR;
            rc = NGX_HTTP_FORBIDDEN;

        } else {
            level = NGX_LOG_CRIT;
            rc = NGX_HTTP_INTERNAL_SERVER_ERROR;
        }

        ngx_log_error(level, r->connection->log, err,
                      ngx_open_file_n " \"%s\" failed", user_file.data);

        return rc;
    }

    ngx_memzero(&file, sizeof(ngx_file_t));

    file.fd = fd;
    file.name = user_file;
    file.log = r->connection->log;

    state = sw_login;
    passwd = 0;
    login = 0;
    left = 0;
    offset = 0;

    for ( ;; ) {
        i = left;

        n = ngx_read_file(&file, buf + left, NGX_HTTP_AUTH_BUF_SIZE - left,
                          offset);

        if (n == NGX_ERROR) {
            rc = NGX_HTTP_INTERNAL_SERVER_ERROR;
            goto cleanup;
        }

        if (n == 0) {
            break;
        }

        for (i = left; i < left + n; i++) {
            switch (state) {

            case sw_login:
                if (login == 0) {

                    if (buf[i] == '#' || buf[i] == CR) {
                        state = sw_skip;
                        break;
                    }

                    if (buf[i] == LF) {
                        break;
                    }
                }

                if (buf[i] != r->headers_in.user.data[login]) {
                    state = sw_skip;
                    break;
                }

                if (login == r->headers_in.user.len) {
                    state = sw_passwd;
                    passwd = i + 1;
                }

                login++;

                break;

            case sw_passwd:
                if (buf[i] == LF || buf[i] == CR || buf[i] == ':') {
                    buf[i] = '\0';

                    pwd.len = i - passwd;
                    pwd.data = &buf[passwd];

                    rc = ngx_http_auth_basic_crypt_handler(r, &pwd, &realm);
                    goto cleanup;
                }

                break;

            case sw_skip:
                if (buf[i] == LF) {
                    state = sw_login;
                    login = 0;
                }

                break;
            }
        }

        if (state == sw_passwd) {
            left = left + n - passwd;
            ngx_memmove(buf, &buf[passwd], left);
            passwd = 0;

        } else {
            left = 0;
        }

        offset += n;
    }

    if (state == sw_passwd) {
        pwd.len = i - passwd;
        pwd.data = ngx_pnalloc(r->pool, pwd.len + 1);
        if (pwd.data == NULL) {
            rc = NGX_HTTP_INTERNAL_SERVER_ERROR;
            goto cleanup;
        }

        ngx_cpystrn(pwd.data, &buf[passwd], pwd.len + 1);

        rc = ngx_http_auth_basic_crypt_handler(r, &pwd, &realm);
        goto cleanup;
    }

    ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                  "user \"%V\" was not found in \"%s\"",
                  &r->headers_in.user, user_file.data);

    rc = ngx_http_auth_basic_set_realm(r, &realm);

cleanup:

    if (ngx_close_file(file.fd) == NGX_FILE_ERROR) {
        ngx_log_error(NGX_LOG_ALERT, r->connection->log, ngx_errno,
                      ngx_close_file_n " \"%s\" failed", user_file.data);
    }

    ngx_explicit_memzero(buf, NGX_HTTP_AUTH_BUF_SIZE);

    return rc;
}
