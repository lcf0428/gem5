// #include <vector>
// #include <cstring>
// #include "gem5/m5ops.h"
// #include <atomic>
// #include <mmintrin.h>
// #include <x86intrin.h>

// volatile uint8_t sink;

// int main() {
//     std::vector<uint8_t> page(4096, 0);
//     std::vector<uint8_t> warmup_cache_line(64, 0);
//     for (int i = 0; i < 64; i++) {
//         warmup_cache_line[i] = i;
//     }

//     for (int j = 0; j < 64; j++) {
//         memcpy(page.data() + 64 * j, warmup_cache_line.data(), 64);
//         _mm_clflush(page.data() + j * 64);
//     }

//     std::vector<uint8_t> new_page(4096, 0);

//     memcpy(new_page.data(), warmup_cache_line.data(), 64);
//     _mm_clflush(new_page.data());

//     std::vector<uint8_t> overflow_cache_line(64, 0);

//     for (int i = 0; i < 64; i++) {
//         overflow_cache_line[i] = (i * i);
//     }

//     // std::vector<uint8_t> new_cache_line(64, 0);

//     // for (int i = 0; i < 64; i++) {
//     //     new_cache_line[i] = (i * i);
//     // }

//     std::vector<uint8_t> new_cache_line = {
//         0x19, 0x92, 0x01, 0x76, 00, 00, 00, 00, 
//         0x80, 0xb8, 0x21, 00, 00, 00, 00, 00, 
//         0x1b, 00, 00, 00, 00, 00, 00, 00,
//         0x78, 00, 00, 00, 00, 00, 00, 00, 
//         0x1a, 00, 00, 00, 00, 00, 00, 00, 
//         0xf8, 0xb8, 0x21, 00, 00, 00, 00, 00, 
//         0x1c ,00, 00, 00, 00, 00, 00, 00, 
//         0x08, 00, 00, 00, 00, 00, 00, 00
//     };

//     std::atomic_thread_fence(std::memory_order_seq_cst);

//     for (int i = 0; i < 14; i++) {
//         memcpy(page.data() + i * 64, overflow_cache_line.data(), 64);
//         _mm_clflush(page.data() + i * 64);
//     }
    
//     memcpy(page.data() + 15 * 64, overflow_cache_line.data(), 64);
//     _mm_clflush(page.data() + 15 * 64);

//     std::atomic_thread_fence(std::memory_order_seq_cst);

//     m5_reset_stats(0, 0);
//     sink = page[0];
//     m5_dump_stats(0, 0);
//     // m5_work_end(0, 0);

    

//     return 0;
// }

#include <vector>
#include <cstring>
#include "gem5/m5ops.h"
#include <atomic>
#include <sys/mman.h>
#include <unistd.h>
#include <iostream>
#include <mmintrin.h>
#include <x86intrin.h>

volatile uint8_t sink;
volatile uint8_t aux;

#define OVERFLOW_NUM 16

int main() {

    uint8_t SECRET = 0x1;

    void *ptr = mmap(NULL, 4096, PROT_READ | PROT_WRITE, 
                     MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);

    
    uint8_t* victim_page = static_cast<uint8_t*>(ptr);

    for (int i = 0; i < 4096; i++) {
        victim_page[i] = 0;
    }

    victim_page[0] = SECRET;

    _mm_mfence();

    for (int i = 0; i < 64; i++) {
        _mm_clflush(victim_page + i * 64);
    }

    _mm_mfence();

    std::cout << "the first elem of victim page is " << static_cast<int>(victim_page[0]) << std::endl;
    
    _mm_mfence();

    ptr = mmap(NULL, 4096, PROT_READ | PROT_WRITE, 
                     MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    uint8_t * new_page = static_cast<uint8_t*>(ptr);
    new_page[0] = 0x01;
    aux = new_page[0];

    _mm_clflush(new_page);

    _mm_mfence();

    std::vector<uint8_t> overflow_data(8, 1);

    uint8_t new_data = 0x01;

    std::vector<uint8_t> new_cache_line = {0x18, 0x92, 0x01, 0x76, 00, 00, 00, 00};

    for (int i = 1; i <= OVERFLOW_NUM; i++) {
        memcpy(victim_page + i * 64, overflow_data.data(), 8);
    }

    _mm_mfence();

    for (int i = 1; i <= OVERFLOW_NUM; i++) {
        _mm_clflush(victim_page + i * 64);
    }

    _mm_mfence();
    victim_page[1] = new_data;
    _mm_clflush(victim_page);
    _mm_mfence();

    m5_reset_stats(0, 0);
    _mm_mfence();
    sink = victim_page[1];
    _mm_mfence();
    m5_dump_stats(0, 0);
    m5_exit(1000);

    return 0;
}
