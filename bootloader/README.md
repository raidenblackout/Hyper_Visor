# bootloader

The UEFI loader (`loader.efi`). Reads `hv_features.cfg` and `netlog.cfg` from
the ESP, maps `mvm-hv.bin`, jumps in, and chainloads Windows' `bootmgfw.efi`.

## Layout

- `src/loader.c` — entry point (`EfiMain`), flat-image mapping (via
  `include/flat_loader.h`), chainload.
- `src/loader_hvb.c` — builds the boot block (`hvb_header_t`) handed to the
  hypervisor: features, netlog config, DRAM extent, mailbox addresses.
- `src/loader_log.c` — live-log to `\EFI\mvm\hv-live.log`, plus optional
  dumps of the loader image and `.sys` for post-mortem inspection.
- `src/loader_file.c`, `loader_util.c`, `loader_rt.c` — ESP file I/O and
  utility helpers.
- `src/platform_uefi.cpp` — UEFI-side stubs the shared `hvmem` code needs.
- `net/` — UDP-frame builder, netlog / feature config parsers. **This code is
  shared with `mvm-hv`** — same source, two consumers. See
  `bootloader.vcxproj` and `mvm-hv/mvm-hv.vcxproj`.

## ESP layout after deploy

```
S:\EFI\mvm\
    loader.efi         # UEFI application (BootOrder entry points here)
    mvm-hv.bin         # hypervisor image
    hv_features.cfg    # boot-time toggles (rewritten by 'mvm-ctrl features set')
    netlog.cfg         # UDP net-log config (dest MAC, IP, port)
    hv-live.log        # live boot log written by loader
    hvb_addr.txt       # boot block physical address hint (post-mortem)
```

`hv_features.cfg` uses a human-readable `key = value` format; the exact set of
keys and their defaults are declared in `net/hv_features_cfg.c`.
