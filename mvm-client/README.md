# mvm-client

Client-side library for `mvm` — an object-oriented C++ API over the
`VMMCALL` ABI the hypervisor exposes. Wraps hypervisor detection, process
isolation, memory hiding, and the freestanding micro-VM lifecycle.

Public headers:

- `Hypervisor.h` — `mvm::Hypervisor` (presence probe + raw VMMCALL dispatch)
- `MicroVM.h`    — `mvm::MicroVM` (allocate / load / step / run / destroy)
- `mvm_client.h` — flat C-style entry points and shared enums (also pulled in
  by `mvm_shared.h`, the header shared with `mvm-hv`)

## Integration

### MSBuild / Visual Studio

1. Add `mvm-client\include` and `mvm-hv\include` to **Additional Include
   Directories**.
2. Link against `mvm-client.lib` (`bin\x64\Release\` or `bin\x64\Debug\`).
3. Include headers:
   ```cpp
   #include "Hypervisor.h"
   #include "MicroVM.h"
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

## Payloads

Freestanding micro-VM payloads live under `mvm-payload/` and compile natively
with MSVC. `tools/mvm_encrypt/mvm_encrypt.exe` (built alongside this library)
pre-encrypts a compiled payload with an XOR key and emits a C header suitable
for `#include`-ing directly into your tool. See `mvm-ctrl/build_test_payload.bat`
and `mvm-ctrl/build_bench_payload.bat` for end-to-end examples.
