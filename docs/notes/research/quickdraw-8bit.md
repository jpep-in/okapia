<!--
Copyright (C) 2026  Jonathan Pepin
SPDX-License-Identifier: GPL-3.0-or-later
-->
# Color QuickDraw call paths and 8-bit drawing

This map follows the standard Color QuickDraw procedures in the Apple source
under `reference/supermario/base/SuperMarioProj.1994-02-09`. Edges represent
possible calls, including conditional paths. The live Quadra ROM and System
patches need their own ABI checks before each new native hook is enabled.

## From drawing calls to pixels

```mermaid
flowchart TD
    ScrollRect --> CopyBits
    CopyBits -->|standard current-port path| StdBits
    CopyBits -->|explicit destination paths| StretchBits
    Masks["CopyMask and KopyMask"] --> BitsDevLoop
    StdBits --> BitsDevLoop
    StdBits --> StretchBits
    BitsDevLoop --> StretchBits
    Rects["Rectangle verbs"] --> StdRect --> Rect["DrawRect or FrmRect"] --> StretchBits
    Regions["Region verbs"] --> StdRgn --> DrawRgn --> StretchBits
    Lines["Line and LineTo"] --> StdLine --> DrawLine
    DrawLine -->|horizontal or vertical fast path| StretchBits
    DrawLine -->|scanline path| DrawSlab
    Curves["Arc, oval and round rectangle verbs"] --> CurveProcs["StdArc, StdOval and StdRRect"]
    CurveProcs --> DrawArc --> DrawSlab
    Poly["Polygon verbs"] --> StdPoly
    StdPoly -->|outline| FrPoly --> DrawLine
    StdPoly -->|fill| DrawPoly
    DrawPoly -->|region implementation| DrawRgn
    DrawPoly -->|QuickPolys implementation| DrawSlab
    Text["DrawChar, DrawString and DrawText"] --> StdText --> DrText
    DrText -->|buffer transfer paths| StretchBits
    StretchBits -->|rectangular clip| BitBlt
    StretchBits -->|region clip| RgnBlt
    StretchBits -->|colour conversion at the same size| ScaleBlt
    StretchBits -->|mask, resize or general conversion| Stretch
    Stretch --> stScanLoop
```

The standard procedures are conditional edges. A port can supply a custom
`CQDProcs` entry, and picture playback dispatches drawing commands back through
the drawing API. Keep those dispatches, cursor protection, pen advancement and
the construction of update regions in guest QuickDraw.

| Edge or decision | Apple source |
| --- | --- |
| Scrolling translates the source rectangle, builds a mask region and calls `CopyBits`. It computes and erases the update region afterwards. | `QuickDraw/BitMaps.a:2241` |
| `CopyBits` can dispatch through the port's `bitsProc` or call `StretchBits` itself. `StdBits` and the masked device loop also reach `StretchBits`. | `QuickDraw/BitMaps.a:749`, `:1208`, `:1397`, `:1523`, `:1837` |
| Rectangle and region drawing supply the pattern and clip to `StretchBits`. | `QuickDraw/Rects.a:556`, `:667`, `QuickDraw/Regions.a:271` |
| Line drawing can take a transfer path or emit horizontal spans. Arcs, ovals and round rectangles share curve code. Polygon fill has two source implementations. | `QuickDraw/DrawLine.a:350`, `:621`, `:696`, `QuickDraw/Arcs.a:131`, `QuickDraw/Ovals.a:128`, `QuickDraw/RRects.a:134`, `QuickDraw/DrawArc.a:885`, `QuickDraw/Polygons.a:421`, `:464`, `QuickDraw/QuickPolys.a:814` |
| Standard text rasterizes glyphs and can pass a text buffer to `StretchBits`. | `QuickDraw/Text.a:916`, `:937`, `QuickDraw/DrawText.a:5501`, `:5585` |
| `StretchBits` expands patterns, protects the cursor and selects each transfer path. `ScaleBlt` can decline, in which case QuickDraw may use its general path. | `QuickDraw/Stretch.a:1149`, `:1161`, `:1225`, `:1445`, `:1494`, `:1518`, `:1521`, `:2721` |

## Native primitives for 8-bit destinations

Treat 8 bits as the **destination format**. An indexed source can have its own
palette, and glyphs or masks may arrive as packed 1-bit data. The rows below
define coverage by drawing path. They can share span writers and source
decoders where the guest uses the same pixel rules.

| Primitive | Cases to cover | Integration |
| --- | --- | --- |
| Raster operation on an 8-bit span | Same-palette image source and expanded patterns, including repeat phase. Copy, OR, XOR and BIC with the normal or inverted operand. Match QuickDraw's foreground and background mapping where applicable. | Generalize the existing `BitBlt` hook (`$AB00`). |
| Region-clipped raster operation | Combine the visible, clip and user regions into spans. Reuse the span writer. Preserve source alignment through holes and traversal order for overlapping copies. | Verify the `RgnBlt` hook (`$AB1F`) and its shared frame before enabling pixel writes. |
| Pattern and masked span from shape rasterization | Keep the guest's geometry and use its prepared horizontal span, mask and pattern. Keep the guest calling convention at the return, including the mode dispatcher. | Verify `DrawSlab` (`$AB0B`) separately for line, arc and polygon call sites. |
| Same-size colour conversion | Palette remap between indexed surfaces and eligible direct-colour to indexed conversions. Exercise region clipping, `srcCopy` and `ditherCopy`. | Verify `ScaleBlt` (`$AB96`). It uses the prepared `StretchBits` frame and reports whether it handled the operation (`QuickDraw/ScaleBlt.a:50`, `QuickDraw/Stretch.a:1445`). |
| General scan loop | Cover monochrome foreground/background expansion, bitmap masks and resizing, including combined cases. Match the guest's sampling phase after clipping. Extend to deep masks, transparency and dithering with their guest-defined rules. | Verify `stScanLoop` (`$AB99` in the Apple table), with the conversion, clipping and row state prepared by `StretchBits` (`$AB27`). Start with 1-bit stencil to 8-bit and 8-to-8 resizing as separate fixtures (`QuickDraw/Stretch.a:2717`). |
| Indexed colour arithmetic and highlight | Preserve palette and operation-colour semantics for blend, add/subtract, min/max, and highlight. | Extend the span writer where the needed state has already been resolved. |

Hook IDs are from `OS/DispTable.a:1064`, `:1075`, `:1095`, `:1103`,
`:1214` and `:1217`. The current hooks handle subsets of the first five rows. In the F1ACAD13
ROM and the verified System 7.1 / 7.6.1 StretchBits patch, same-size
RGB32-to-indexed-8 conversion reaches `ScaleBlt`. An indexed-to-indexed
palette mismatch goes through the stretch path. The native `ScaleBlt` uses
an existing device inverse table, while `stScanLoop` currently handles
same-palette 8-bit copies with a 1-bit stencil and exact unmasked 2× and 4×
expansions. Text buffers, cursor paths, PICT opcodes,
and optional System patches need a separate walk to identify drawing paths
outside this graph.

For each native primitive, compare guest and ARM64 output byte for byte in a
test that exercises it directly. Use visible and offscreen surfaces, odd
positions, nonzero bounds, different row strides and clipped edges. Include
overlap in both directions, region holes, indexed palette remap, text masks,
and the fallback from an unrecognized live handler. Run the same fixtures on
System 7.1 and 7.6.1 before using Speedometer as an end-to-end check.
