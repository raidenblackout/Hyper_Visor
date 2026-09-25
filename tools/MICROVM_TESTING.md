# MicroVM fault-handling changes

The Albion inspector was reported to cause an immediate restart without a
Windows bugcheck. The triggering instruction has not been established. These
changes address independently identified runtime defects; they do not constitute
hardware validation or proof that the reported restart is resolved.

## Changes

- Intercept guest exceptions and shutdown. The freestanding MicroVM has no guest
  exception handlers, so faults must return to the host instead of cascading.
- Preserve host and per-instance guest x87, MXCSR, and XMM state across every
  MicroVM entry/exit using aligned FXSAVE64 images. Guest CR4.OSXSAVE remains clear;
  this is an x87/SSE context implementation, not an AVX-enabled guest.
- Advance RIP after the yield VMMCALL, using NRip where available.
- Keep Done and errors terminal until a successful payload load resets the state.
- Retain the last exit code, RIP, and exit information in the instance for debugging.

## Offline checks

Run `python tools/test_microvm_offline.py` with Visual Studio C++ tools installed.
The script copies the actual assembly switch into a temporary user-mode test,
replaces VMLOAD/VMRUN/VMSAVE with a simulated SIMD workload, and rejects any
remaining SVM instruction. It verifies host register preservation and guest state
across repeated simulated steps. It also compiles the actual `step()` body with a
mock entry function to exercise yield and terminal-state behavior, and checks the
configured fault intercepts. No hypervisor calls are made.

Build through the solution:

```powershell
msbuild mvm.sln /t:mvm-hv /p:Configuration=Release /p:Platform=x64 /m
```

Output: `bin/x64/Release/mvm-hv.bin` (flat image) and `mvm-hv.pe` (build intermediate).
Building does not install the image or replace the running hypervisor.

## Staged live checks

After `mvm-ctrl ping 0 1` and `mvm-ctrl microvm` succeed on the updated image,
build the `mvm-ctrl` project in Release x64. Its executable is
`x64/Release/mvm-ctrl.exe`. These additional checks require no hypervisor rebuild.
Run each command separately, proceeding only after the previous command returns
`microvm-check: PASS` and exits successfully:

```powershell
.\x64\Release\mvm-ctrl.exe microvm-check yield
.\x64\Release\mvm-ctrl.exe microvm-check fault
```

The yield payload performs three explicit yields, checks XMM6 and an accumulated
XMM7 value after resuming, then signals Done. The client checks that another step
still returns Done. The fault payload deliberately executes UD2; the client
expects -1 twice, then reloads a Done payload into the same VM and checks that it
can complete. The current API returns a generic error, so the fault check cannot
independently identify the exact guest exception or prove absence of re-entry.
The offline runtime checks cover the terminal-state entry guard directly.

Neither payload accesses system or game memory. Each run has a 64-call step
budget, which cannot impose a timeout on a hypercall that does not return.
Progress is flushed before each hypervisor call. For a persistent console log,
start a PowerShell transcript before testing. These checks do not establish
overall hypervisor stability or validate the Albion payload.

`python tools/test_microvm_check_offline.py` tests the new CLI using a fake
backend, including timeout, failure, reload, and cleanup handling. It does not
link the hypercall client or execute the embedded guest code.

## Before another Albion capture

Review and deploy the image through the existing bootloader workflow, retaining
a known working boot option, then explicitly reboot into that image. First test a
minimal payload that does no process inspection, followed by controlled yield,
SIMD-resume, and intentional guest-fault checks with external logs. Offline tests
do not establish interrupt delivery, real SVM entry/exit behavior, or recovery on
this hardware. The Albion snapshot packing defect must also be fixed before
trusting full capture output. Do not use the inspector as the initial runtime test.
