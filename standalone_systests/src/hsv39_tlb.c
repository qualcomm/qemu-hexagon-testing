/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <hexagon_standalone.h>
#include "hsv39.h"

void test_page_size(int pgsize_id)
{
    uint64_t page_size = pow4(__builtin_ctzll(pgsize_id)) * 1024 * 1024;
    uint64_t addr = (uint64_t)&data;
    uint64_t page = page_start64(addr, page_size);
    uint64_t offset = 4ull * 1024 * 1024 * 1024;
    uint64_t virt_addr = addr + offset;
    uint64_t virt_page = page + offset;
    int asid = 1, index = 512; /* HSV39 DMA TLB entries start from 512 */
    mmu_func_t f = func_return_pc;
    mmu_func_t new_f;
    printf("Testing page size 0x%llx\n", page_size);

    add_hsv39_tlb_entry(index, virt_page, page, pgsize_id, 0xf, asid, 1, 1);
    check32(tlbp64(asid, virt_addr), index);

    /* Load through the new VA */
    data = 0xdeadbeef;
    check32(*(mmu_variable *)virt_addr, 0xdeadbeef);

    /* Store through the new VA */
    *(mmu_variable *)virt_addr = 0xcafebabe;
    check32(data, 0xcafebabe);

    /* Clear out this entry */
    remove_hsv39_trans(index);
    check32(tlbp64(asid, virt_addr), TLB_NOT_FOUND);

    /* Set up a mapping for function execution */
    addr = (uint32_t)f;
    page = page_start64(addr, page_size);
    virt_page = page + offset;
    virt_addr = addr + offset;
    index++;
    add_hsv39_tlb_entry(index, virt_page, page, pgsize_id, 0xf, asid, 1, 1);
    check32(tlbp64(asid, virt_addr), index);

    /*
     * Call the function at the new address
     * It will return it's PC, which should be the new address
     */
    new_f = (mmu_func_t)virt_addr;
    check32((new_f()), (int)virt_addr);

    /* Clear out this entry */
    remove_hsv39_trans(index);
    check32(tlbp64(asid, virt_addr), TLB_NOT_FOUND);
}

/* Verify that tlbp64 only searches extended entries, not legacy ones. */
void test_tlbp()
{
    /* Add legacy entry */
    uint32_t addr = (uint32_t)&data;
    uint32_t page = page_start(addr, TARGET_PAGE_BITS);
    uint32_t offset = ONE_MB;
    uint32_t virt_addr = addr + offset;
    uint32_t virt_page = page + offset;
    int index = 1, asid = 1;
    add_translation_extended(index, (void *)virt_page, page,
                             PAGE_1M, 0xf, 0x7, asid, 0, 0x3);

    check32(tlbp(asid, virt_addr), index);
    check32(tlbp64(asid, virt_addr), TLB_NOT_FOUND);

    remove_trans(index);
}

/* tlbr should be able to read both legacy and extended entries */
void test_tlbr()
{
    /* Add extended entry */
    int pgsize_id = HSV39_PAGE_1G;
    uint64_t page_size = pow4(__builtin_ctzll(pgsize_id)) * 1024 * 1024;
    uint64_t addr = (uint64_t)&data;
    uint64_t page = page_start64(addr, page_size);
    uint64_t offset = 4ull * 1024 * 1024 * 1024;
    uint64_t virt_addr = addr + offset;
    uint64_t virt_page = page + offset;
    int asid = 1, index = 512; /* HSV39 DMA TLB entries start from 512 */
    TLBEntry64 ext_entry = add_hsv39_tlb_entry(index, virt_page, page,
                                               pgsize_id, 0xf, asid, 1, 1);

    /* Add legacy entry */
    uint32_t leg_addr = (uint32_t)&data;
    uint32_t leg_page = page_start(leg_addr, TARGET_PAGE_BITS);
    uint32_t leg_offset = ONE_MB;
    uint32_t leg_virt_addr = leg_addr + leg_offset;
    uint32_t leg_virt_page = leg_page + leg_offset;
    int leg_index = 1;
    uint64_t leg_entry = create_mmu_entry(1, 0, 0, asid, leg_virt_page, 1, 1,
                                          1, 0, 7, leg_page, PAGE_4K);
    tlbw(leg_entry, leg_index);

    check64(tlbr(index), ext_entry.raw);
    check64(tlbr(leg_index), leg_entry);

    remove_hsv39_trans(index);
    remove_trans(leg_index);
}

int main()
{
    puts("Hexagon HSV39 MMU page size test");

    test_page_size(HSV39_PAGE_1M);
    test_page_size(HSV39_PAGE_4M);
    test_page_size(HSV39_PAGE_16M);
    test_page_size(HSV39_PAGE_256M);
    test_page_size(HSV39_PAGE_1G);
    test_page_size(HSV39_PAGE_4G);
    test_page_size(HSV39_PAGE_16G);
    test_page_size(HSV39_PAGE_64G);

    test_tlbp();
    test_tlbr();

    printf("%s\n", ((err) ? "FAIL" : "PASS"));
    return err;
}
