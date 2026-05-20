#include <vector>
#include <cstring>
#include "gem5/m5ops.h"
#include <atomic>
#include <sys/mman.h>
#include <unistd.h>
#include <iostream>
#include <mmintrin.h>
#include <x86intrin.h>

volatile uint64_t sink;
// volatile uint8_t sink;
volatile uint8_t aux;

#define OVERFLOW_NUM 16
#define N 40960

int main() {

    void *ptr = mmap(NULL, 4096, PROT_READ | PROT_WRITE, 
                     MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);

    
    uint8_t* victim_page = static_cast<uint8_t*>(ptr);

    for (int i = 0; i < 64; i++) {
        victim_page[i] = 1;
    }

    for (int i = 0; i < 64; i++) {
        victim_page[64 + i] = i * i;
    }

    // for (int i = 0; i < 8; i++) {
    //     victim_page[64 + i] = 1;
    // }

    // for (int i = 0; i < 56; i++) {
    //     victim_page[72 + i] = i * i;
    // }

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

    m5_reset_stats(0, 0);
    _mm_mfence();
    // sink = victim_page[64];
    for (int i = 0; i < N; i++) {
        _mm_clflush(victim_page + 64);
        _mm_mfence();
        sink = *(volatile uint64_t*)(victim_page + 64);
    }
    // sink = victim_page[64];
    _mm_mfence();
    m5_dump_stats(0, 0);

    _mm_mfence();

    return 0;
}
