/*
 *  imgenc_soft.c - the encoders without the 68881: the reference bytes
 *
 *  Copyright (C) 2026  Jonathan Pepin
 *  SPDX-License-Identifier: MIT
 */
#define IMGENC_JPEG jpegEncodeSoft
#define IMGENC_PNG pngEncode
#include "imgenc.inc"
