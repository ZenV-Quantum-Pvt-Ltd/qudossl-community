# QUDO Runtime CPU Dispatch Extension

This directory contains QUDO-specific additions to mlkem-native for runtime CPU optimization dispatch.

## Purpose

Enables mlkem-native to automatically select the best implementation (AVX2, NEON, or portable C) at runtime based on CPU capabilities, similar to how liboqs works.

## Files

- `cpu_features.h` - CPU feature detection API
- `cpu_features.c` - Cross-platform CPU detection (x86_64, ARM64, Windows, Linux, macOS)
- `README.md` - This file

## Why Separate Folder?

This code is kept separate from upstream mlkem-native source to:
- Clearly identify QUDO additions vs original mlkem-native code
- Make it easy to update mlkem-native without conflicts
- Allow simple inclusion/exclusion via CMake options
- Facilitate potential upstream contribution

## Integration

When `MLK_CONFIG_RUNTIME_DISPATCH=ON`:
1. This code gets compiled into libmlkem_native.a
2. mlkem-native dispatch points call `mlk_cpu_has_extension()`
3. Wrapper calls `mlk_cpu_init()` once at startup
4. All variants (AVX2 + NEON + C) included in one library

## Maintenance

- Keep synced when updating mlkem-native
- If upstreamed, this folder can be removed
- No modifications to original mlkem-native files within this directory
