#pragma once
#include <windows.h>

int cmd_vmexit_dump(int argc, LPWSTR* argv);
int cmd_unload(int argc, LPWSTR* argv);

int cmd_microvm(int argc, LPWSTR* argv);
int cmd_microvm_check(int argc, LPWSTR* argv);
int cmd_microvm_test(int argc, LPWSTR* argv);
int cmd_microvm_bench(int argc, LPWSTR* argv);

int cmd_ping(int argc, LPWSTR* argv);
int cmd_ping_all(int argc, LPWSTR* argv);

int cmd_netlog(int argc, LPWSTR* argv);

int cmd_features(int argc, LPWSTR* argv);
