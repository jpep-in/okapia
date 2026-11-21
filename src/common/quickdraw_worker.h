/*
 * Copyright (C) 2026  Jonathan Pepin
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#pragma once

struct QDBlit;
void QDStartWorker ();
void QDStopWorker ();
bool QDParallelBlit (const QDBlit &blit);
