/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include <stdint.h>
#include <stdio.h>

#define VM_VERSION      0x00
#define VM_SETREGS      0x15
#define VM_GETREGS      0x16
#define VM_GETINFO      0x1A

static inline uint32_t vm_instruction(uint32_t op)
{
    uint32_t result;
    asm volatile("trap1(#%1)" : "=r"(result) : "i"(op));
    return result;
}

static inline void vm_setregs(uint32_t r0, uint32_t r1, uint32_t r2, uint32_t r3)
{
    asm volatile("r0 = %0; r1 = %1; r2 = %2; r3 = %3; trap1(#0x15)"
                 : : "r"(r0), "r"(r1), "r"(r2), "r"(r3)
                 : "r0", "r1", "r2", "r3");
}

static inline void vm_getregs(uint32_t *r0, uint32_t *r1, uint32_t *r2, uint32_t *r3)
{
    asm volatile("trap1(#0x16); %0 = r0; %1 = r1; %2 = r2; %3 = r3"
                 : "=r"(*r0), "=r"(*r1), "=r"(*r2), "=r"(*r3)
                 : : "r0", "r1", "r2", "r3");
}

int main()
{
    uint32_t version, info;
    uint32_t r0, r1, r2, r3;
    
    printf("Testing Hexagon VM implementation...\n");
    
    /* Test vmversion */
    version = vm_instruction(VM_VERSION);
    printf("VM Version: 0x%x\n", version);
    if (version != 0x800) {
        printf("FAIL: Expected version 0x800, got 0x%x\n", version);
        return 1;
    }
    
    /* Test vmgetinfo */
    info = vm_instruction(VM_GETINFO);
    printf("VM Build ID: 0x%x\n", info);
    
    /* Test guest register operations */
    vm_setregs(0x12345678, 0xabcdef00, 0x11223344, 0x55667788);
    vm_getregs(&r0, &r1, &r2, &r3);
    
    printf("Guest regs: r0=0x%x r1=0x%x r2=0x%x r3=0x%x\n", r0, r1, r2, r3);
    
    if (r0 == 0x12345678 && r1 == 0xabcdef00 && 
        r2 == 0x11223344 && r3 == 0x55667788) {
        printf("PASS: All VM tests passed!\n");
        return 0;
    } else {
        printf("FAIL: Guest register test failed\n");
        return 1;
    }
}
