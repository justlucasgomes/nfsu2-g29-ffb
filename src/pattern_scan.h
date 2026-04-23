#pragma once
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdint>
#include <string>

// Pattern scanner for NFSU2 in-process memory.
// Used to dynamically locate game data pointers at runtime.
namespace PatternScan {

    // Scan [start, start+size) for pattern with optional wildcards ('?').
    // Pattern string format: "55 8B EC ?? ?? 8B 45" (space-separated hex, '??' = any byte)
    uintptr_t Scan(uintptr_t start, size_t size, const char* pattern);

    // Scan the main module (.exe) for a pattern.
    uintptr_t ScanModule(const char* pattern);

    // Dereference a pointer at addr (4-byte ptr, 32-bit game).
    uintptr_t Deref(uintptr_t addr);

    // Follow a chain of offsets: base → +off[0] → +off[1] → ...
    // Returns 0 if any step is null/unmapped.
    uintptr_t Follow(uintptr_t base, const DWORD* offsets, int count);

    // Safe float read with __try/__except guard. Returns defaultVal on AV.
    float SafeReadFloat(uintptr_t addr, float defaultVal = 0.0f);

    // Validate that addr points to readable mapped memory.
    bool IsReadable(uintptr_t addr, size_t bytes = 4);

} // namespace PatternScan
