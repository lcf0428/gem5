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

#define EXHAUSTED_NUM 300

int main() {

    void *ptr = mmap(NULL, 4096, PROT_READ | PROT_WRITE, 
                     MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);

    
    uint8_t* victim_page = static_cast<uint8_t*>(ptr);

    for (int i = 0; i < 4096; i++) {
        victim_page[i] = 0;
    }

    victim_page[0] = 41;


    // for (int i = 0; i < 64; i++) {
    //     for (int j = 0; j < 64; j++) {
    //         victim_page[i * 64 + j] = (i * 64 + j) % (17 + i);
    //     }
    // }

    _mm_mfence();

    for (int i = 0; i < 64; i++) {
        _mm_clflush(victim_page + i * 64);
    }

    void *e_ptr[EXHAUSTED_NUM];

    for (int i = 0; i < EXHAUSTED_NUM; i++) {
        e_ptr[i] = mmap(NULL, 4096, PROT_READ | PROT_WRITE, 
                     MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    }

    for (int i = 0; i < EXHAUSTED_NUM; i++) {
        uint64_t* uint64_p = (uint64_t*)e_ptr[i];
        uint64_p[0] = i + 1; 
    }

    for (int i = 0; i < EXHAUSTED_NUM; i++) {
        _mm_clflush(e_ptr[i]);
    }

    _mm_mfence();

    m5_reset_stats(0, 0);
    // _mm_mfence();
    sink = victim_page[1];
    // _mm_mfence();
    m5_dump_stats(0, 0);

    return 0;
}
