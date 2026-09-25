# pe2mvmf

Post-build tool: convert the linker's PE output for `mvm-hv` into the
**MVM Flat (MVMF)** binary the UEFI loader actually consumes.

## Why

MSVC's linker emits `mvm-hv.pe` as a PE image because that is what the
compiler produces; nothing in the boot chain treats it as a real DLL,
either. The PE headers, export directory, section table, and reloc
directory are all overhead the bootloader used to parse by hand.
`pe2mvmf` throws away that overhead and emits a tight flat blob:

```
+--------------------------+  offset 0
| mvm_flat_header (40 B)   |
+--------------------------+  offset 40
| image bytes (image_size) |  section-unpacked, bss zero-filled
+--------------------------+  offset reloc_off
| uint32_t rvas[reloc_cnt] |  each = image RVA of a DIR64 slot
+--------------------------+
```

Only `IMAGE_REL_BASED_DIR64` and `IMAGE_REL_BASED_ABSOLUTE` (padding) are
tolerated in the PE reloc directory. Any other reloc type is a hard error —
if MSVC ever starts emitting a new type, we want to know at build time, not
in the loader.

The header layout is kept in exact byte-level lockstep with
`bootloader/include/flat_loader.h`.

## Usage

Invoked automatically by the `Pe2Mvmf` post-build target in
`mvm-hv/mvm-hv.vcxproj`. Manual invocation:

```
py -3 tools/pe2mvmf/pe2mvmf.py <input.pe> <output.bin> [--entry <name>]
```

Default entry symbol is `hvruntime_boot_entry`. The tool looks it up in the
PE export directory and stores its RVA in the flat header — the loader
doesn't need any name resolution at runtime.

## Requirements

Python 3.10+ (stdlib only: `struct`, `argparse`, `pathlib`). No third-party
PE library — parsing is inline and stops at exactly what the loader needs.
