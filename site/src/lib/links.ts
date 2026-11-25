// Copyright (C) 2026 Jonathan Pepin
// SPDX-License-Identifier: GPL-3.0-or-later
export const base = import.meta.env.BASE_URL.replace(/\/$/, '');
export const link = (path = '') => `${base}/${path.replace(/^\//, '')}`;
export const repository = 'https://github.com/jpep-in/okapia';
