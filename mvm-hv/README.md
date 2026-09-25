# mvm-hv

The mvm hypervisor image. Built as a `DynamicLibrary` under the
`WindowsKernelModeDriver10.0` platform toolset — the toolset supplies the
KM headers and lib paths (no-CRT, kernel typedefs, `ntoskrnl.lib` /
`hal.lib` / `wmilib.lib`), and `DynamicLibrary` gets us a plain PE with
base relocations, without dragging in any driver-package plumbing (no
KMDF, no `DriverEntry`, no `.inf`, no `DriverSign`, no signing step).
`EntryPointSymbol` is set to `hvruntime_boot_entry` directly.

The linker emits `mvm-hv.pe` as a build-time intermediate; the post-build
step `tools/pe2mvmf/pe2mvmf.py` converts that into the flat `mvm-hv.bin`
that the bootloader actually loads. Nothing about `mvm-hv` is a Windows
driver — the OS never sees it.

## Layout

- `src/core/` — SVM init and per-CPU bring-up, NPT setup, VMEXIT dispatch, MSR
  handler, LAPIC/x2APIC intercepts, AP wake-up, host exception handlers,
  DRAM-extent probe, teardown path.
- `src/vmm/` — `VMMCALL` handler (the control-plane the `mvm-ctrl` CLI and
  `mvm-client` library talk to).
- `src/micro_vm/` — freestanding-guest launcher: allocates a fresh VMCB and
  NPT, maps the payload, and runs it under host supervision.
- `src/memory/hvmem/` — the shared hypervisor allocator (UEFI + kernel
  variants); the exact same source is compiled into both `bootloader` and
  `mvm-hv`.
- `src/asm/` — `vm_launch.asm` (VMRUN loop), `vm_intrin.asm` (VMSAVE/VMLOAD/
  CLGI/STGI), `exception_handlers.asm`.
- `src/utils/` — logging, panic-to-framebuffer, spinlocks, PCI + RTL8125 for
  net-log, platform stubs.
- `include/svm/` — architectural SVM structs (`SvmVmcb`, MSR constants, paging
  types, CPUID indices). These are **arch-named** and stay `Svm*` even after
  the project rename.
- `include/mvm_shared.h` — types shared with `mvm-client` / `mvm-ctrl` across
  the VMMCALL boundary (`mvm_microvm_load_t`, `mvm_microvm_done_signal`,
  the preshared VMMCALL key).

## Boot flow

1. UEFI firmware loads `\EFI\mvm\loader.efi`.
2. `loader.efi` reads `hv_features.cfg` and `netlog.cfg` from the ESP, maps
   `mvm-hv.bin`, hands it a boot block (`hvb_header_t`), and jumps in.
3. `mvm-hv` probes SVM, sets up NPT, brings up APs (up to 16 cores with SMT
   off — see the notes on the 32-core / SMT-on firmware crash), and turns
   into a guest supervisor.
4. `loader.efi` chainloads `\EFI\Microsoft\Boot\bootmgfw.efi` and Windows
   boots normally, now virtualized.

## Debug channel

Screen-only. No serial, no POST LEDs, and DRAM is wiped on reset. Panic /
diagnostic output goes to the framebuffer (`fb_panic`); optional UDP net-log
via RTL8125 (`mvm-ctrl features set netlog 1`).
