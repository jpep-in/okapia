/*
 * Copyright (C) 2026  Jonathan Pepin
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#pragma once

struct M68kRegisters;

constexpr unsigned QD_EMUL_OP = 0x71fe;
void QDReset ();
void QDInstall ();
void QDEmulOp (M68kRegisters &regs);
void QDSetScreen (unsigned char *pixels, unsigned size);
void QDReport ();
