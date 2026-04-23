#include "pattern_scan.h"
#include "logger.h"
#include <vector>
#include <sstream>
#include <stdexcept>

namespace PatternScan {

// ── Pattern parser ─────────────────────────────────────────────────────────

struct PatternByte { uint8_t value; bool wildcard; };

static std::vector<PatternByte> ParsePattern(const char* pattern) {
    std::vector<PatternByte> bytes;
    std::istringstream ss(pattern);
    std::string token;
    while (ss >> token) {
        if (token == "?" || token == "??") {
            bytes.push_back({0, true});
        } else {
            bytes.push_back({static_cast<uint8_t>(std::stoul(token, nullptr, 16)), false});
        }
    }
    return bytes;
}

// ── Core scanner ───────────────────────────────────────────────────────────

uintptr_t Scan(uintptr_t start, size_t size, const char* pattern) {
    auto pat = ParsePattern(pattern);
    if (pat.empty() || size < pat.size()) return 0;

    const uint8_t* mem  = reinterpret_cast<const uint8_t*>(start);
    const size_t   last = size - pat.size();

    for (size_t i = 0; i <= last; ++i) {
        bool found = true;
        for (size_t j = 0; j < pat.size(); ++j) {
            if (!pat[j].wildcard && mem[i + j] != pat[j].value) {
                found = false;
                break;
            }
        }
        if (found) return start + i;
    }
    return 0;
}

uintptr_t ScanModule(const char* pattern) {
    HMODULE hMod = GetModuleHandleA(nullptr);
    if (!hMod) return 0;

    // Walk PE sections
    auto* dosHdr = reinterpret_cast<IMAGE_DOS_HEADER*>(hMod);
    auto* ntHdr  = reinterpret_cast<IMAGE_NT_HEADERS*>(
                       reinterpret_cast<uint8_t*>(hMod) + dosHdr->e_lfanew);

    uintptr_t  imageBase = reinterpret_cast<uintptr_t>(hMod);
    size_t     imageSize = ntHdr->OptionalHeader.SizeOfImage;

    LOG_DEBUG("PatternScan: scanning 0x%08X + 0x%X for [%s]",
              (DWORD)imageBase, (DWORD)imageSize, pattern);

    uintptr_t result = Scan(imageBase, imageSize, pattern);
    if (result)
        LOG_DEBUG("PatternScan: found at 0x%08X", (DWORD)result);
    else
        LOG_DEBUG("PatternScan: NOT found [%s]", pattern);

    return result;
}

// ── Memory helpers ─────────────────────────────────────────────────────────

uintptr_t Deref(uintptr_t addr) {
    if (!IsReadable(addr, sizeof(DWORD))) return 0;
    return *reinterpret_cast<uintptr_t*>(addr);
}

uintptr_t Follow(uintptr_t base, const DWORD* offsets, int count) {
    uintptr_t cur = base;
    for (int i = 0; i < count; ++i) {
        cur = Deref(cur);
        if (!cur) return 0;
        cur += offsets[i];
    }
    return cur;
}

float SafeReadFloat(uintptr_t addr, float defaultVal) {
    if (!IsReadable(addr, sizeof(float))) return defaultVal;
    __try {
        return *reinterpret_cast<const float*>(addr);
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        return defaultVal;
    }
}

bool IsReadable(uintptr_t addr, size_t bytes) {
    if (!addr) return false;
    MEMORY_BASIC_INFORMATION mbi{};
    if (!VirtualQuery(reinterpret_cast<LPCVOID>(addr), &mbi, sizeof(mbi))) return false;
    if (mbi.State != MEM_COMMIT) return false;
    if (mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD)) return false;
    // Check the range fits inside the queried region
    uintptr_t regionEnd = reinterpret_cast<uintptr_t>(mbi.BaseAddress) + mbi.RegionSize;
    return (addr + bytes) <= regionEnd;
}

} // namespace PatternScan
