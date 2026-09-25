# mvm-payload

Freestanding micro-VM payloads. Each `*.mvm.cpp` compiles to a standalone,
position-independent code blob suitable for XOR-encrypting and loading into
a fresh guest via `MicroVM::load()`.

Payloads may only touch:

- The `microvm_rt` runtime (`include/microvm_rt.h`) — a thin surface that
  exposes VMMCALL slots (`microvm_vmmcall.asm`) and the shared config
  (`include/microvm_config.h`).
- Their own stack and heap-in-code-region.

They may **not** link against libcrt, use exceptions, or take any external
Windows dependency. Any symbol the runtime doesn't provide will be a link
error against the payload's minimal link order (`link_order.txt`).

## Building

```
build_payload.bat <source.mvm.cpp> <output_name>
```

`mvm-ctrl` ships two pre-built payloads for end-to-end verification:

- `tests/test_all.mvm.cpp` — sanity + smoke tests
- `tests/bench.mvm.cpp`    — timing benches

Both are pre-encrypted by `mvm_encrypt.exe` (from `mvm-client/tools/`) at
build time and included as C headers by `mvm-ctrl` — the encryption key never
touches the ESP.
