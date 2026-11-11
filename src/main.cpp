/**
 * @file main.cpp
 * @brief Phase 1 PoC: Target architecture, ABI validation, and runtime sanity check.
 * 
 * This test binary verifies cross-compilation correctness for Rockchip RV1103 (ARM Cortex-A7),
 * checks system page size, memory layout structures, and hardware floating-point capabilities.
 */

#include <iostream>
#include <iomanip>
#include <unistd.h>
#include <sys/sysinfo.h>

void print_system_architecture() {
    std::cout << "========================================================\n";
    std::cout << "  Smart Edge Camera - Phase 1 Target System Validation   \n";
    std::cout << "========================================================\n";

#if defined(__arm__)
    std::cout << "[TARGET ARCH] Architecture       : ARM 32-bit\n";
#else
    std::cout << "[TARGET ARCH] Architecture       : NON-ARM (Unexpected!)\n";
#endif

#if defined(__ARM_ARCH_7A__)
    std::cout << "[TARGET ARCH] Core Profile       : ARMv7-A (Cortex-A7 Compatible)\n";
#endif

#if defined(__ARM_NEON) || defined(__ARM_NEON__)
    std::cout << "[HARDWARE ACC] SIMD Engine       : ARM NEON Supported\n";
#else
    std::cout << "[HARDWARE ACC] SIMD Engine       : ARM NEON Not Enabled\n";
#endif

#if defined(__ARM_PCS_VFP)
    std::cout << "[HARDWARE ACC] Floating Point ABI: Hard Float (VFPv4 / VFP-D16)\n";
#else
    std::cout << "[HARDWARE ACC] Floating Point ABI: Soft Float / Softfp\n";
#endif

    std::cout << "\n--- Memory & Data Model Inspection ---\n";
    std::cout << "Pointer Size                     : " << sizeof(void*) * 8 << "-bit\n";
    std::cout << "size_t Size                      : " << sizeof(size_t) * 8 << "-bit\n";
    std::cout << "System Page Size                 : " << sysconf(_SC_PAGESIZE) << " bytes\n";

    struct sysinfo s_info;
    if (sysinfo(&s_info) == 0) {
        constexpr unsigned long MB = 1024 * 1024;
        std::cout << "\n--- Linux Kernel Memory Statistics ---\n";
        std::cout << "Total Physical RAM Reported      : " << (s_info.totalram * static_cast<unsigned long>(s_info.mem_unit)) / MB << " MB\n";
        std::cout << "Free RAM Available               : " << (s_info.freeram * static_cast<unsigned long>(s_info.mem_unit)) / MB << " MB\n";
        std::cout << "Shared RAM (Buffers/CMA etc.)    : " << (s_info.sharedram * static_cast<unsigned long>(s_info.mem_unit)) / MB << " MB\n";
        std::cout << "Buffer RAM                       : " << (s_info.bufferram * static_cast<unsigned long>(s_info.mem_unit)) / MB << " MB\n";
    } else {
        std::cerr << "[ERROR] Unable to retrieve sysinfo details.\n";
    }

    std::cout << "========================================================\n";
}

int main() {
    print_system_architecture();
    return 0;
}
