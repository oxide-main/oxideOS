#include "memory_test.h"
#include "pmm.h"
#include "heap.h"
#include "paging.h"
#include "common_headers/string.h"

extern uint8_t _kernel_start;
extern uint8_t _kernel_end;

int memory_run_tests(void)
{
    uintptr_t f1 = pmm_alloc_frame();
    if (f1 == 0) return 0;
    if ((f1 % PAGE_SIZE) != 0) return 0;
    if (f1 < PAGE_SIZE) return 0;

    uintptr_t k_start = (uintptr_t) &_kernel_start;
    uintptr_t k_end   = (uintptr_t) &_kernel_end;
    if (f1 >= k_start && f1 < k_end) return 0;

    uintptr_t f2 = pmm_alloc_frame();
    uintptr_t f3 = pmm_alloc_frame();
    if (f2 == 0 || f3 == 0) return 0;
    if (f1 == f2 || f2 == f3 || f1 == f3) return 0;

    pmm_free_frame(f2);
    uintptr_t f4 = pmm_alloc_frame();
    if (f4 != f2) return 0;

    pmm_free_frame(f1);
    pmm_free_frame(f3);
    pmm_free_frame(f4);

    uintptr_t mp = pmm_alloc_frames(4);
    if (mp == 0 || (mp % PAGE_SIZE) != 0) return 0;
    pmm_free_range(mp, 4);

    size_t used_before = pmm_used_frames();
    pmm_free_frame(0);
    pmm_free_frame(k_start);
    if (pmm_used_frames() != used_before) return 0;

    uint8_t *p1   = (uint8_t *) kmalloc(1);
    uint8_t *p16  = (uint8_t *) kmalloc(16);
    uint8_t *p64  = (uint8_t *) kmalloc(64);
    uint8_t *p256 = (uint8_t *) kmalloc(256);
    uint8_t *p4k  = (uint8_t *) kmalloc(4096);

    if (!p1 || !p16 || !p64 || !p256 || !p4k) return 0;

    if (((uintptr_t) p1 & 7) != 0) return 0;
    if (((uintptr_t) p16 & 7) != 0) return 0;
    if (((uintptr_t) p64 & 7) != 0) return 0;
    if (((uintptr_t) p256 & 7) != 0) return 0;
    if (((uintptr_t) p4k & 7) != 0) return 0;

    *p1 = 0xAA;
    if (*p1 != 0xAA) return 0;
    memset(p16, 0x11, 16);
    memset(p64, 0x22, 64);
    memset(p256, 0x33, 256);
    memset(p4k, 0x44, 4096);

    for (int i = 0; i < 16; i++) {
        if (p16[i] != 0x11) return 0;
    }
    for (int i = 0; i < 64; i++) {
        if (p64[i] != 0x22) return 0;
    }
    for (int i = 0; i < 256; i++) {
        if (p256[i] != 0x33) return 0;
    }
    for (int i = 0; i < 4096; i++) {
        if (p4k[i] != 0x44) return 0;
    }

    kfree(p1);
    kfree(p16);
    kfree(p64);
    kfree(p256);
    kfree(p4k);

    kfree(0);

    void *a = kmalloc(100);
    void *b = kmalloc(200);
    void *c = kmalloc(300);
    if (!a || !b || !c) return 0;

    kfree(b);

    void *d = kmalloc(100);
    if (d != b) return 0;

    kfree(a);
    kfree(d);
    kfree(c);

    void *ca = kmalloc(128);
    void *cb = kmalloc(128);
    void *cc = kmalloc(128);
    if (!ca || !cb || !cc) return 0;

    kfree(ca);
    kfree(cb);
    void *c_large = kmalloc(256);
    if (c_large != ca) return 0;
    kfree(c_large);
    kfree(cc);

    ca = kmalloc(128);
    cb = kmalloc(128);
    cc = kmalloc(128);
    if (!ca || !cb || !cc) return 0;

    kfree(cb);
    kfree(ca);
    c_large = kmalloc(256);
    if (c_large != ca) return 0;
    kfree(c_large);
    kfree(cc);

    ca = kmalloc(128);
    cb = kmalloc(128);
    cc = kmalloc(128);
    if (!ca || !cb || !cc) return 0;

    kfree(cb);
    kfree(cc);
    c_large = kmalloc(256);
    if (c_large != cb) return 0;
    kfree(c_large);
    kfree(ca);

    void *ptrs[6];
    for (int i = 0; i < 6; i++) {
        ptrs[i] = kmalloc(2048);
        if (!ptrs[i]) return 0;
        memset(ptrs[i], (uint8_t)(i + 1), 2048);
    }
    for (int i = 0; i < 6; i++) {
        uint8_t *byte_ptr = (uint8_t *) ptrs[i];
        for (int j = 0; j < 2048; j++) {
            if (byte_ptr[j] != (uint8_t)(i + 1)) return 0;
        }
        kfree(ptrs[i]);
    }

    for (int iter = 0; iter < 20; iter++) {
        void *tmp = kmalloc(64 + iter * 8);
        if (!tmp) return 0;
        memset(tmp, 0x5A, 64 + iter * 8);
        kfree(tmp);
    }

    uint32_t *zeros = (uint32_t *) kcalloc(32, sizeof(uint32_t));
    if (!zeros) return 0;
    for (int i = 0; i < 32; i++) {
        if (zeros[i] != 0) return 0;
        zeros[i] = i * 7 + 1;
    }
    for (int i = 0; i < 32; i++) {
        if (zeros[i] != (uint32_t)(i * 7 + 1)) return 0;
    }
    kfree(zeros);

    uint8_t *re_buf = (uint8_t *) kmalloc(16);
    if (!re_buf) return 0;
    for (int i = 0; i < 16; i++) {
        re_buf[i] = (uint8_t)(i + 0x40);
    }
    re_buf = (uint8_t *) krealloc(re_buf, 128);
    if (!re_buf) return 0;
    for (int i = 0; i < 16; i++) {
        if (re_buf[i] != (uint8_t)(i + 0x40)) return 0;
    }
    kfree(re_buf);

    void *huge = kmalloc(0xFFFFFFFF);
    if (huge != 0) return 0;
    uintptr_t huge_frames = pmm_alloc_frames(0xFFFFFFFF);
    if (huge_frames != 0) return 0;

    void *df_ptr = kmalloc(64);
    if (!df_ptr) return 0;
    kfree(df_ptr);
    kfree(df_ptr);

    void *post_df = kmalloc(64);
    if (!post_df) return 0;
    kfree(post_df);

    static const uint32_t stress_sizes[9] = { 1, 8, 16, 32, 128, 512, 4096, 8192, 16384 };
    void *stress_ptrs[9];

    for (int cycle = 0; cycle < 3; cycle++) {
        for (int i = 0; i < 9; i++) {
            stress_ptrs[i] = kmalloc(stress_sizes[i]);
            if (!stress_ptrs[i]) return 0;
            if (((uintptr_t) stress_ptrs[i] & 7) != 0) return 0;

            uint8_t *b = (uint8_t *) stress_ptrs[i];
            uint8_t pat = (uint8_t) (0x55 + i + cycle);
            memset(b, pat, stress_sizes[i]);
            for (uint32_t j = 0; j < stress_sizes[i]; j++) {
                if (b[j] != pat) return 0;
            }
        }

        for (int i = 0; i < 9; i++) {
            kfree(stress_ptrs[i]);
            stress_ptrs[i] = 0;
        }
    }

    if (paging_is_enabled()) {
        if (!paging_run_tests()) {
            return 0;
        }
    }

    return 1;
}
