# mvm-ctrl

Control CLI for the mvm hypervisor. Depends on `mvm-client` for the VMMCALL
transport and on ESP-write helpers here for persisting configuration.

## Commands

Run `mvm-ctrl help` for the full list. Highlights:

- `ping` / `ping-all` — round-trip a VMMCALL from one or every logical CPU.
  Useful to confirm virtualization is live on every core.
- `features get [<key>]` — dump runtime and boot-time toggles.
  Runtime keys are queried from the HV live over VMMCALL; boot-time keys are
  read from `\EFI\mvm\hv_features.cfg` on the ESP.
- `features set <key> <value>` — flip a toggle. Runtime toggles take effect
  immediately and are persisted to the ESP for the next boot. Boot-time keys
  only apply after a reboot.
- `netlog on|off|status` — toggle the UDP net-log (also mirrored as the
  `netlog` feature key).
- `vmexit-dump <path>` — drain the HV's VMEXIT ring buffer to a file.
- `microvm <payload> [args...]` — run a payload inside a fresh micro-VM.
- `microvm-test`, `microvm-bench` — run the bundled test / bench payloads
  (pre-encrypted at build time; see `build_test_payload.bat` /
  `build_bench_payload.bat`).
- `unload` — ask the HV to tear itself down (`VMMCALL_HV_UNLOAD`).

## Exit codes

- `0` — success
- `1` — HV missing when a command required it, or command-specific failure
- `2` — usage error (unknown command, missing arg, etc.)
- `3` — ESP write / persistence failure

## Elevation

`features` and `netlog` write to the ESP via a diskpart-driven mount, so
`mvm-ctrl` must be run from an elevated console.
