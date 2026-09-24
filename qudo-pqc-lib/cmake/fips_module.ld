/*
 * SPDX-License-Identifier: Apache-2.0 AND MIT
 *
 * fips_module.ld — Supplemental linker script for FIPS 140-3 module boundary.
 *
 * Ensures FIPS code boundary markers (qudo_fips_module_start/end) are
 * placed at the beginning and end of the .text section respectively.
 *
 * This script is used via -Wl,-T,fips_module.ld (supplemental, not replacement).
 * It works WITH the default linker script, not instead of it.
 *
 * The SORT_NONE directive preserves input order within the .text section,
 * ensuring boundary_start.o code appears first and boundary_end.o code
 * appears last. This is critical for the in-memory integrity check:
 * the region from &qudo_fips_module_start to &qudo_fips_module_end must
 * contain all FIPS crypto code and nothing else.
 *
 * Build requirement: compile with -fno-function-sections to keep all
 * functions in .text (not .text.function_name).
 *
 * FIPS 140-3 (ISO/IEC 19790 Section 7.10.3.3): module boundary definition.
 */

/*
 * Supplemental SECTIONS block — INSERT keeps it additive to the default
 * GNU ld script. We define a tiny output section .qudo_fipsanchor
 * immediately before .text that KEEPs the boundary-start sentinel,
 * plus a matching anchor-end section immediately after .text. The
 * bulk of FIPS code lives in the default .text section, already
 * ordered by source-file linking order (boundary_start.c first,
 * boundary_end.c last, enforced via -fno-function-sections). The
 * anchor sections ensure the sentinel functions cannot be dead-stripped
 * or folded by link-time optimisation, regardless of toolchain version.
 */

SECTIONS
{
    .qudo_fipsanchor_start : ALIGN(16)
    {
        KEEP(*qudo_fips_boundary_start.c.o(.text .text.*))
    }
}
INSERT BEFORE .text;

SECTIONS
{
    .qudo_fipsanchor_end : ALIGN(16)
    {
        KEEP(*qudo_fips_boundary_end.c.o(.text .text.*))
    }
}
INSERT AFTER .text;
