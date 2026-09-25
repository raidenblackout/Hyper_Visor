# mvm — micro-VM hypervisor

`mvm` is a bare-metal AMD-SVM hypervisor that launches out of UEFI, virtualizes
Windows across every core, and exposes a `VMMCALL`-based control interface for
running short-lived, freestanding **micro-VMs** — small payloads that execute
inside a fresh guest with their own address space and isolated stack.

It is not a general-purpose hypervisor; it is a research vehicle for isolated
execution, memory hiding, and process observation on stock consumer AMD parts.

Everything runs from the ESP — there is no driver-mode load path on this
branch, and the driver's `DriverEntry` stub is intentionally
`STATUS_NOT_IMPLEMENTED`.

## Layout

| Project        | What it is                                                            |
| -------------- | --------------------------------------------------------------------- |
| [mvm-hv/](mvm-hv/) | The hypervisor itself — kernel-format PE, loaded from UEFI, never registered as a Windows driver. Handles SVM init, NPT setup, VMEXIT dispatch, VMMCALL handlers, and the micro-VM runtime. |
| [bootloader/](bootloader/) | The UEFI loader (`loader.efi`). Reads `hv_features.cfg` / `netlog.cfg` from the ESP, maps and jumps into `mvm-hv.bin`, then chainloads `bootmgfw.efi`. Shares memory-manager and net-config code with `mvm-hv`. |
| [mvm-payload/](mvm-payload/) | Freestanding micro-VM payloads (`.mvm.cpp`) — the tiny guest programs the HV runs on demand. Bundled tests and bench live under [mvm-payload/tests/](mvm-payload/tests/). |
| [mvm-client/](mvm-client/) | C++ client library (`mvm-client.lib`) that wraps the `VMMCALL` ABI as an object-oriented API (`mvm::Hypervisor`, `mvm::MicroVM`). Ships `mvm_encrypt.exe` for pre-encrypting payloads at build time. |
| [mvm-ctrl/](mvm-ctrl/) | Control CLI (`mvm-ctrl.exe`). Ping the HV, toggle features, drain the VMEXIT ring, dispatch a micro-VM. Depends on `mvm-client`. |
| [tools/](tools/) | Standalone diagnostics + build tools. `hv_check` — CPUID-based presence check across every logical processor. `pe2mvmf` — post-build script that converts `mvm-hv.pe` into the flat MVMF blob the loader consumes. `bin2h.py` / `extract_text_and_bin2h.py` — payload → C-header converters. |
| [scripts/](scripts/) | Deploy scripts. [deploy-loader.ps1](scripts/deploy-loader.ps1) mounts the ESP as `S:`, drops `loader.efi` and `mvm-hv.bin` under `\EFI\mvm\`, and pulls back live logs. |

## Prerequisites

- **Hardware**: AMD Zen 4 (or any AMD part with SVM + NPT). See *Constraints*
  for the tested envelope.
- **OS**: Windows 10/11 x64 with UEFI boot and an accessible ESP (unencrypted —
  no BitLocker on the boot volume).
- **Toolchain**: Visual Studio 2022 (Community is fine) with:
  - Desktop development with C++ (MSVC v143, Windows 10/11 SDK)
  - Windows Driver Kit (WDK 10) — provides the
    `WindowsKernelModeDriver10.0` toolset that `mvm-hv` builds against
  - Python 3.10+ on `PATH` (stdlib only) — used by `pe2mvmf` and the
    payload `bin2h` converters
- **Elevation**: deploying and running `mvm-ctrl` requires an elevated
  console (both mount the ESP via `diskpart`).

## Build

Use MSBuild against the **solution** — not the individual `.vcxproj` — so that
`$(SolutionDir)` resolves for the shared `hvb.h` include.

```powershell
# From a "x64 Native Tools Command Prompt for VS" or with MSBuild on PATH.
msbuild mvm.sln -p:Configuration=Release -p:Platform=x64 -m
```

Artifacts:

| File | Where |
| --- | --- |
| `mvm-hv.bin`      | `bin/x64/Release/` — hypervisor image, in MVM Flat (MVMF) format |
| `mvm-hv.pe`       | `bin/x64/Release/` — build-time PE intermediate (not deployed) |
| `loader.efi`      | `bin/x64/Release/` — UEFI loader |
| `mvm-client.lib`  | `bin/x64/Release/` — client library |
| `mvm-ctrl.exe`    | `x64/Release/`     — control CLI |
| `mvm_encrypt.exe` | `x64/Release/`     — payload pre-encrypter |
| `deploy-loader.ps1` | `bin/x64/Release/` — mirrored from `scripts/` so it can find sibling build outputs |

The `mvm-hv` project builds as a `DynamicLibrary` under the
`WindowsKernelModeDriver10.0` platform toolset — the toolset is what wires
up the kernel-mode header and lib paths (no-CRT, `ntoskrnl.lib` /
`hal.lib` / `wmilib.lib`), and `DynamicLibrary` gets us a plain PE with
base relocations without the driver-package plumbing (no KMDF, no
`DriverEntry`, no `.inf`, no `DriverSign`, no signing). The linker's PE
output is `mvm-hv.pe`; a post-build step ([tools/pe2mvmf/pe2mvmf.py](tools/pe2mvmf/pe2mvmf.py))
strips the PE overhead and emits `mvm-hv.bin` — a compact flat image with
a 40-byte header, the section-unpacked bytes, and a list of `DIR64`
relocation RVAs. The bootloader consumes only the `.bin`.

## Deploy

From an **elevated** PowerShell (`scripts/deploy-loader.ps1` is mirrored into
`bin/x64/Release/` at build time so it can find its inputs — running either
copy works):

```powershell
scripts\deploy-loader.ps1
# or, after a build:
bin\x64\Release\deploy-loader.ps1
```

What it does:

1. Assigns drive letter `S:` to ESP volume 2 via `diskpart`. **Verify volume
   number 2 is your ESP on your machine** — edit the `select volume 2` line
   in the script if not.
2. Creates `S:\EFI\mvm\` if missing, then copies:
   - `loader.efi`
   - `mvm-hv.bin`
   - `netlog.cfg` (if present in the build dir)
3. Snapshots any live logs from the ESP back into
   `bin/x64/Release/logs/<timestamp>/` (and mirrors the newest one to
   `logs/latest/`) — `hv-live.log`, `hvb_addr.txt`. (The loader image
   dumps `sbrun.bin` / `mod2.bin` and the `hvb_prev_mailbox.txt` scratch
   file are no longer pulled.)

Flags:

- `-LogsOnly` — skip the file copies and only pull logs back.
- `-BuildDir <path>` — override the source directory (defaults to
  `$PSScriptRoot` when running from the build output, otherwise
  `../bin/x64/Release`).

### Making the firmware launch mvm

Copying the files is not enough — you also have to point the firmware at
`\EFI\mvm\loader.efi`. Before touching `bcdedit`, get the platform ready:

1. **Disable Secure Boot** in your firmware setup (F2 / Del at power-on).
   `loader.efi` and `mvm-hv.bin` are unsigned; Secure Boot will refuse them.
2. **Disable BitLocker** on the boot volume
   (`manage-bde -off C:`, wait for decryption to finish). The HV changes the
   boot chain in a way BitLocker's PCR sealing treats as tamper.
3. **Disable Fast Startup** (Control Panel → Power Options → Choose what the
   power buttons do → Change settings that are currently unavailable →
   uncheck "Turn on fast startup"). Fast Startup resumes from a hibernation
   image and skips the full boot chain, so the loader never runs.

Two ways to actually launch the loader:

#### Path A — Firmware boot menu (fast, per-boot, safe)

Reboot into the vendor's one-shot boot menu (F12 / F10 / Esc / F9 depending
on OEM), pick "UEFI OS" or the ESP's `\EFI\mvm\loader.efi` entry, and boot
that. Nothing is persisted — the next reboot goes straight back to Windows
Boot Manager. Use this while you're still iterating on the loader; if
`loader.efi` crashes, just power-cycle and boot normally.

#### Path B — Persistent boot entry via `bcdedit` (survives reboots)

Preferred over hijacking `{bootmgr}` because it leaves the Windows Boot
Manager entry intact — if the loader misbehaves, you can pick Windows Boot
Manager from the firmware menu and be fine. From an **elevated cmd** (not
PowerShell — the `{...}` braces confuse PowerShell's parser without extra
quoting):

```cmd
:: 1. Clone the Windows Boot Manager entry as a starting point.
::    bcdedit prints "The entry was successfully copied to {new-guid}."
::    Copy that GUID.
bcdedit /copy {bootmgr} /d "mvm loader"

:: 2. Point the new entry at loader.efi on the ESP.
bcdedit /set {NEW-GUID-FROM-STEP-1} path \EFI\mvm\loader.efi

:: 3. Put the new entry first in the firmware boot order so it runs on
::    the next boot without needing the boot menu.
bcdedit /set {fwbootmgr} displayorder {NEW-GUID-FROM-STEP-1} /addfirst

:: 4. (Optional) Give yourself a boot-menu timeout so you can still pick
::    Windows Boot Manager without going into firmware setup.
bcdedit /set {fwbootmgr} timeout 3
```

Verify:

```cmd
bcdedit /enum firmware
```

You should see two entries — `Windows Boot Manager` and your new
`mvm loader` — with the new one listed first under `{fwbootmgr}`
`displayorder`.

To reverse it (return to Windows-only boot):

```cmd
:: Remove the mvm entry from the firmware display order and delete it.
bcdedit /set {fwbootmgr} displayorder {NEW-GUID-FROM-STEP-1} /remove
bcdedit /delete {NEW-GUID-FROM-STEP-1} /f
```

If you'd rather **not** create a new entry and just hijack the default:

```cmd
bcdedit /set "{bootmgr}" path \EFI\mvm\loader.efi
:: reverse:
bcdedit /set "{bootmgr}" path \EFI\Microsoft\Boot\bootmgfw.efi
```

Faster to set, but if the loader crashes you're staring at a firmware-menu
detour to fix it — do it only after Path A has confirmed the loader boots
on your machine.

#### Emergency off-switches

- The loader honours `virtualize = 0` in `hv_features.cfg` on the ESP —
  set it and reboot, and the loader will skip SVM setup and chainload
  Windows unchanged (loader still runs, HV does not).
- If the ESP is reachable from a WinPE or another OS, deleting
  `\EFI\mvm\loader.efi` makes the firmware entry fall through; combine
  with removing the entry via `bcdedit` (or restoring `{bootmgr}` path)
  to fully undo Path B.
- `mvm-ctrl unload` asks a running HV to tear itself down without a
  reboot, once you're back in Windows.

## Control from Windows

Once the HV is live, from an elevated console:

```powershell
mvm-ctrl                        # default: report HV presence
mvm-ctrl ping                   # single VMMCALL from cpu 0
mvm-ctrl ping-all               # sweep every logical processor
mvm-ctrl features get           # dump runtime and boot-time toggles
mvm-ctrl features set fb_log 1  # flip a runtime toggle and persist to ESP
mvm-ctrl netlog on              # enable UDP net-log
mvm-ctrl vmexit-dump run.log    # drain the VMEXIT ring
mvm-ctrl microvm-test           # run the bundled test payload in a micro-VM
mvm-ctrl microvm-bench          # run the bundled bench payload
mvm-ctrl microvm <blob> [args]  # run an arbitrary payload
mvm-ctrl unload                 # ask the HV to tear itself down
mvm-ctrl help                   # full command list
```

Exit codes: `0` success, `1` HV missing / command failure, `2` usage error,
`3` ESP write failure.

`tools/hv_check/hv_check.cpp` (built as `hv_check.exe`) reads CPUID
`0x40000000` on every logical processor, expecting the vendor slot to read
`mvm ` (a 12-byte string — three characters plus padding, occupying
`EBX|ECX|EDX`). Use it when `mvm-ctrl ping-all` disagrees with itself and
you want a second opinion straight out of `CPUID`.

## Hypervisor features

Toggles fall into two groups. **Runtime** keys are flipped over `VMMCALL`
and take effect on the next call — `mvm-ctrl features set` also writes the
new value to the ESP so it survives the next boot. **Boot-time** keys are
stored only in `\EFI\mvm\hv_features.cfg`; the loader reads them before
handing off to the HV, so a reboot is required for a change to matter. The
canonical schema lives in [bootloader/net/hv_features_cfg.h](bootloader/net/hv_features_cfg.h)
and the defaults in [bootloader/net/hv_features_cfg.c](bootloader/net/hv_features_cfg.c).

### Runtime toggles

| Key       | Default | What it does |
| --------- | ------- | ------------ |
| `fb_log`  | `1`     | Framebuffer panic / trace output — the primary post-`ExitBootServices` debug channel. Turning it off silences the on-screen log. |
| `log_mem` | `1`     | In-HV memory-log ring capture. Feeds `mvm-ctrl vmexit-dump` and the trace subsystem. |
| `netlog`  | *(from `netlog.cfg`)* | UDP net-log over the RTL8125 NIC. Configured by `netlog.cfg` on the ESP (dest MAC / IP / port); `mvm-ctrl netlog on\|off\|status` or `mvm-ctrl features set netlog 1` toggles the live send-arm state. |

### Boot-time toggles

| Key                     | Default | What it does |
| ----------------------- | ------- | ------------ |
| `virtualize`            | `1`     | Master switch. `0` makes `loader.efi` skip SVM setup and chainload Windows unchanged — the emergency off-switch when the HV misbehaves and you can't get into firmware. |
| `boot_menu_timeout_ms`  | `5000`  | How long the loader's "Press ESC to boot Windows without HV" prompt waits before proceeding. Set `0` to skip the prompt entirely. |
| `intercept_shutdown`    | `1`     | Trap guest ACPI shutdown / reset (SVM `INTERCEPT_SHUTDOWN`) so the HV can tear down cleanly instead of getting yanked mid-flight. |
| `intercept_pause`       | `1`     | Trap `PAUSE` (SVM `INTERCEPT_PAUSE`). Enables the pause-loop-exit heuristic; disabling it removes that source of VMEXITs. |
| `intercept_svm_guard`   | `1`     | Trap the guest's own SVM MSR / instruction attempts (`VMRUN`, `VMLOAD`, `VMSAVE`, `STGI`, `CLGI`, `EFER.SVME` writes). Leaving this on prevents a guest that happens to enumerate SVM from stepping on the host. |

### Reading and writing them

```powershell
mvm-ctrl features get                          # dump every key with its live value
mvm-ctrl features get intercept_pause          # just one key
mvm-ctrl features set fb_log 0                 # runtime: immediate + persisted
mvm-ctrl features set boot_menu_timeout_ms 0   # boot-time: written, needs reboot
```

`get` on a runtime key issues a live `VMMCALL` and reports what the HV
actually has right now; on a boot-time key it reports what
`hv_features.cfg` on the ESP says (which is what the *next* boot will
pick up). Values `on` / `off` / `true` / `false` / `1` / `0` are all
accepted. Unrecognized keys are refused with exit code `2`.

You can also edit `\EFI\mvm\hv_features.cfg` by hand — it's a
human-readable `key = value` file, with `#` comments; missing keys fall
back to the compile-time defaults above.

## Building your own tool against mvm-client

`mvm-client` gives you an object-oriented C++ wrapper over the `VMMCALL` ABI —
you don't have to write the inline assembly or hand-roll the shared enums.

### MSBuild / Visual Studio

1. Add `mvm-client\include` and `mvm-hv\include` to **Additional Include
   Directories**.
2. Link against `bin\x64\Release\mvm-client.lib` (or the Debug variant).
3. Include and use:

   ```cpp
   #include "Hypervisor.h"
   #include "MicroVM.h"

   int wmain()
   {
       mvm::Hypervisor hv;
       if (!hv.Connect()) return 1;      // no HV, or wrong build

       mvm::MicroVM vm(hv, /*memory_size=*/64 * 1024 * 1024);
       if (!vm.IsValid()) return 2;

       // Payload data typically comes from a header emitted by mvm_encrypt.
       extern const unsigned char my_payload_bin[];
       extern const unsigned int  my_payload_bin_len;
       if (!vm.LoadPayload(my_payload_bin, my_payload_bin_len)) return 3;

       int64_t rc = vm.Run(/*max_steps=*/1'000'000);
       return static_cast<int>(rc);
   }
   ```

### CMake

```cmake
cmake_minimum_required(VERSION 3.10)
project(MyHypervisorTool)

add_executable(my_tool main.cpp)

target_include_directories(my_tool PRIVATE
    ${HV_ROOT}/mvm-client/include
    ${HV_ROOT}/mvm-hv/include
)

target_link_libraries(my_tool PRIVATE
    ${HV_ROOT}/bin/x64/Release/mvm-client.lib
)
```

Your tool must run **elevated** — every VMMCALL happens on the caller's
thread, and the driver-less design means the transport goes straight
through `CPUID` / `VMMCALL` from user mode with the pre-shared key baked
into `mvm_shared.h`.

## Writing your own micro-VM payload

A payload is a freestanding, position-independent C++ blob that only touches
the `mvm::` runtime surface exposed by [mvm-payload/include/microvm_rt.h](mvm-payload/include/microvm_rt.h).
No CRT, no exceptions, no Windows imports.

The runtime lets you translate VAs against another process's CR3, walk PEB /
module lists, pattern-scan guest memory, allocate from a code-region heap,
signal completion, and yield the vCPU:

```cpp
#include "microvm_rt.h"

extern "C" void _start(mvm::MicroVmConfig* cfg)
{
    mvm::Init(cfg);

    uint64_t cr3 = mvm::FindFirstProcessByName("notepad.exe");
    if (cr3)
    {
        uint64_t base = mvm::FindModuleBase(cr3, "kernel32.dll");
        auto*    mb   = static_cast<uint64_t*>(mvm::GetMailbox(0));
        mb[0] = base;
    }

    mvm::Done();  // hand the vCPU back to the HV
}
```

### Build

Use the wrapper batch file — it invokes `cl`/`ml64`/`link` with the exact
flags the HV expects (`/SUBSYSTEM:NATIVE`, `/NODEFAULTLIB`, `/ALIGN:4096`,
custom `/ENTRY:_start`, custom base):

```
mvm-payload\build_payload.bat my_payload.mvm.cpp my_payload
```

Output:

- `my_payload.exe`   — the linked PE (only its `.text` matters)
- `my_payload.mvm.h` — a C header with `my_payload_bin[]` / `_bin_len`,
  produced by `tools/extract_text_and_bin2h.py`

### Encrypt (required)

The HV refuses **plaintext** payloads: it expects the blob XOR'd with the
per-tool key that `mvm_encrypt.exe` writes into `<name>.mvm_key.h`. Encrypt
before shipping:

```
x64\Release\mvm_encrypt.exe my_payload.mvm.h
```

The encryption is done at build time — the key never touches the ESP; it
travels only inside your controller `.exe`. See
[mvm-ctrl/build_test_payload.bat](mvm-ctrl/build_test_payload.bat) and
[mvm-ctrl/build_bench_payload.bat](mvm-ctrl/build_bench_payload.bat) for
end-to-end examples the solution already runs as pre-build steps.

### Dispatch

Include both headers in your controller `.exe`, hand the buffer to
`MicroVM::LoadPayload()` with the XOR key, then `Run()`. `mvm-ctrl
microvm-test` and `microvm-bench` are the shipped worked examples — read
[mvm-ctrl/src/cmd_microvm.cpp](mvm-ctrl/src/cmd_microvm.cpp) for the
canonical flow.

### Preemption via host interrupts

Every MicroVM VMCB is created with the AMD SVM physical-INTR intercept on
(`InterceptMisc1` bit 0, set once in
[mvm-hv/src/micro_vm/micro_vm.cpp:207](mvm-hv/src/micro_vm/micro_vm.cpp#L207)).
While a MicroVM is running, any physical interrupt that would land on the
core — root kernel timer tick, IPI, device IRQ — is promoted to
`VMEXIT_INTR`, control returns from `VMRUN`, and `micro_vm::step()`
returns `0` back to the controlling driver. The interrupt itself is left
pending on the physical APIC and delivered to the root guest as normal
on the next root `VMRUN`. Nothing new is programmed; the exits are
paths the platform already generates.

This gives the driver a natural preemption edge on every host tick: the
step loop is guaranteed to periodically hand control back, which is
enough to bound MicroVM wall time at the granularity of the root's own
interrupt rate. Verified on Windows-as-root with:

```powershell
mvm-ctrl microvm-check intr
```

which runs a tight `pause; jmp $` guest with no VMMCALL / fault / memory
path — the only exits it can generate are `VMEXIT_INTR`. On a stock
Windows root it reports `steps=128 intr=128 vintr=0 done=0 yield=0
other=0`, i.e. every step returned via a real physical interrupt.

The granularity is whatever the platform happens to fire on that core.
Windows default is ~64 Hz (≈15 ms); raised timer resolution pushes it to
~1 kHz. This is **coarse preemption**: fine (<15 ms) budgets, or
defending against a payload that runs `cli`, require the HV to own the
clock via a LAPIC one-shot / `IA32_TSC_DEADLINE` — not currently done.

## Debug channel

There is no serial and no POST-LED channel on the primary target. After
`ExitBootServices`, the only reliable diagnostic is framebuffer output
(`fb_panic`) plus the UDP net-log (`mvm-ctrl netlog on`, or set the
`netlog` feature key). See [mvm-hv/README.md](mvm-hv/README.md) for the
per-subsystem breakdown and [bootloader/README.md](bootloader/README.md)
for the ESP layout the loader writes.

## Constraints

- **AMD SVM only.** No Intel VMX path exists.
- **Consumer AMD parts (Zen 4).** AP virtualization is enabled on up to
  16 cores with SMT off; the 32-core / SMT-on firmware crash is a known
  open issue (see `docs/AP_VIRTUALIZATION_FINDINGS.md` if you have it).
- **No driver-mode load.** `DriverEntry` is a stub. Load the HV from
  `loader.efi` or don't load it at all.
- **No BitLocker.** The HV modifies the boot chain in a way BitLocker's
  PCR sealing will treat as tamper.
- **Elevation required.** `mvm-ctrl`, `deploy-loader.ps1`, and any tool
  built against `mvm-client` all need an elevated console — the ESP mount
  and the raw `VMMCALL` path both demand it.
