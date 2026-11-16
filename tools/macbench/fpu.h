/*
 *  fpu.h - MacBench's floating-point tests (fpu.c)
 *
 *  Copyright (C) 2026  Jonathan Pepin
 *  SPDX-License-Identifier: MIT
 */
#ifndef MACBENCH_FPU_H
#define MACBENCH_FPU_H

unsigned long tFpuFft(void);
unsigned long tFpuWhet(void);
unsigned long tFpuMatrix(void);
unsigned long tFpuTrig(void);
unsigned long tFpuMove(void);

#endif
