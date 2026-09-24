// SPDX-License-Identifier: Apache-2.0
// QUDO Runtime CPU Dispatch Extension for mlkem-native
#ifndef QUDO_CPU_FEATURES_H
#define QUDO_CPU_FEATURES_H

#include <stdint.h>

/**
 * CPU feature flags for runtime optimization dispatch
 */
typedef enum {
    MLK_CPU_EXT_INIT = 0,     /* Initialization marker */
    MLK_CPU_EXT_AVX2,         /* x86_64 AVX2 */
    MLK_CPU_EXT_NEON,         /* ARM NEON */
    MLK_CPU_EXT_COUNT         /* Must be last */
} mlk_cpu_ext;

/**
 * Initialize CPU feature detection
 * Call this once at program startup
 */
void mlk_cpu_init(void);

/**
 * Check if CPU has specific extension
 * @param ext The CPU extension to check
 * @return 1 if available, 0 otherwise
 */
int mlk_cpu_has_extension(mlk_cpu_ext ext);

/**
 * Get OS name as string
 * @return OS name (Linux, Windows, macOS, etc.)
 */
const char* mlk_cpu_get_os(void);

/**
 * Get architecture name as string
 * @return Architecture name (x86_64, ARM64, etc.)
 */
const char* mlk_cpu_get_arch(void);

/**
 * Print system information and detected CPU features
 */
void mlk_cpu_print_info(void);

#endif /* QUDO_CPU_FEATURES_H */
