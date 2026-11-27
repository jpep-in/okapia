/*
 *  emb_config.h - included before every Embench source (CMakeLists.txt)
 *
 *  Copyright (C) 2026  Jonathan Pepin
 *  SPDX-License-Identifier: MIT
 *
 *  Embench takes its global scale factor as a constant from its build; here
 *  it is a variable, so that each benchmark runs long enough to be timed on
 *  a fast host and not too long on a Macintosh of the time (emb_port.c).
 *  The official score divides by it, so the results stay comparable.
 */
#ifndef OKAPIABENCH_EMB_CONFIG_H
#define OKAPIABENCH_EMB_CONFIG_H
extern unsigned int emb_gsf;
#define GLOBAL_SCALE_FACTOR emb_gsf
#define WARMUP_HEAT 1
#endif
