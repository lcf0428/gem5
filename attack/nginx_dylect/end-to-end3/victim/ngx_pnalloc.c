#include "types.h"
#include <stdlib.h>

/* Forward declarations */
static void *ngx_palloc_block(ngx_pool_t *pool, size_t size);
static void *ngx_palloc_large(ngx_pool_t *pool, size_t size);
static ngx_inline void *ngx_palloc_small(ngx_pool_t *pool, size_t size,
    ngx_uint_t align);

static void *
ngx_palloc_large(ngx_pool_t *pool, size_t size)
{
    void             *p;
    ngx_pool_large_t *large;

    p = malloc(size);
    if (p == NULL) return NULL;

    large = malloc(sizeof(ngx_pool_large_t));
    if (large == NULL) {
        free(p);
        return NULL;
    }

    large->alloc = p;
    large->next  = pool->large;
    pool->large  = large;

    return p;
}

static void *
ngx_palloc_block(ngx_pool_t *pool, size_t size)
{
    u_char      *m;
    size_t       psize;
    ngx_pool_t  *p, *new;

    psize = (size_t) (pool->d.end - (u_char *) pool);

    m = (u_char *) memalign(NGX_POOL_ALIGNMENT, psize);
    if (m == NULL) return NULL;

    new = (ngx_pool_t *) m;
    new->d.end    = m + psize;
    new->d.next   = NULL;
    new->d.failed = 0;

    m += sizeof(ngx_pool_data_t);
    new->d.last = m + size;

    for (p = pool->current; p->d.next; p = p->d.next) {
        if (p->d.failed++ > 4) {
            pool->current = p->d.next;
        }
    }
    p->d.next = new;

    return m;
}

void *
ngx_pnalloc(ngx_pool_t *pool, size_t size)
{
#if !(NGX_DEBUG_PALLOC)
    if (size <= pool->max) {
        return ngx_palloc_small(pool, size, 0);
    }
#endif

    return ngx_palloc_large(pool, size);
}


static ngx_inline void *
ngx_palloc_small(ngx_pool_t *pool, size_t size, ngx_uint_t align)
{
    u_char      *m;
    ngx_pool_t  *p;

    p = pool->current;

    do {
        m = p->d.last;

        if (align) {
            m = ngx_align_ptr(m, NGX_ALIGNMENT);
        }

        if ((size_t) (p->d.end - m) >= size) {
            p->d.last = m + size;
            return m;
        }

        p = p->d.next;

    } while (p);

    return ngx_palloc_block(pool, size);
}
