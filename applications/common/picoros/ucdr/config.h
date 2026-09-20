/*
 * Micro-CDR build configuration.
 *
 * Upstream generates this from include/ucdr/config.h.in via its own CMake
 * project, which this workspace does not run (see picoros.cmake for why).
 * The only thing the generated header carries that Micro-CDR's sources
 * actually read is the machine endianness, so derive it from Zephyr's own
 * Kconfig instead of from a configure-time probe. CONFIG_BIG_ENDIAN comes
 * from autoconf.h, which Zephyr force-includes into every translation unit.
 *
 * UCDR_LITTLE_ENDIANNESS / UCDR_BIG_ENDIANNESS are enumerators declared in
 * <ucdr/microcdr.h> *after* it includes this header; that is fine because
 * the macro is only expanded at its use sites, which are further down still.
 */

#ifndef _MICROCDR_CONFIG_H_
#define _MICROCDR_CONFIG_H_

#ifdef CONFIG_BIG_ENDIAN
#define UCDR_MACHINE_ENDIANNESS UCDR_BIG_ENDIANNESS
#else
#define UCDR_MACHINE_ENDIANNESS UCDR_LITTLE_ENDIANNESS
#endif

#endif /* _MICROCDR_CONFIG_H_ */
