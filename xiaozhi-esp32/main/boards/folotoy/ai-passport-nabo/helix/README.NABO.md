# Helix fixed-point MP3 decoder source

Copied on 2026-10-13 from the existing Tab5 component
`chmorgan__esp-libhelix-mp3/libhelix-mp3`, originally developed by
RealNetworks. The source and original copyright/license notices are retained.

The decoder is **RPSL-covered code**, with the alternative RCSL text retained
as supplied upstream. It is not relicensed as Apache, MIT, or CC0. See the
complete `LICENSE.txt`, `RPSL.txt`, and `RCSL.txt` files, including the notices
preserved in the original source subdirectories. Other project components
retain their own licensing terms.

Only root decoder C sources, `pub` headers, `real` C sources/headers, the
original readme, and every original LICENSE/RPSL/RCSL file were copied. Sample
programs, toolchain projects, assembly source files, and other documentation
were omitted; their license files remain included.

NABO modification to `real/assembly.h`, dated 2026-10-13: an explicitly selected
`NABO_HELIX_PORTABLE` path provides fixed-point C primitives for host regression
tests. The original RISC-V `mulh`/`mul` path is unchanged and is selected by
normal ESP32-C3 builds. The original license block and prominent modification
notice remain in that file. This modification is distributed under the same
RPSL terms.

Firmware object-code notice: **Helix DNA Client technology included. Copyright
(c) RealNetworks, Inc., 1995-2002. All rights reserved.** Corresponding covered
source and licenses accompany the project in `third_party/helix-mp3`. Any
subsequent distribution must preserve the applicable source, object-code, and
modification notices required by the original license.

Build the root `*.c` and `real/*.c`, with `pub` and `real` include paths.
No managed-component download or external assembly file is needed on ESP32-C3.
