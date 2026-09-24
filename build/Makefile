# QudoSSL top-level build wrapper
#
#   make -C build            build everything
#   make -C build qudo-pqc   stage 1 only (libqudo-pqc.a)
#   make -C build openssl    stage 2 only (libcrypto.so, libssl.so, fips.so)
#   make -C build test       run the OpenSSL test suite
#   make -C build clean      remove build outputs
#   make -C build help       list targets
#
# Two stages:
#   1. CMake builds qudo-pqc-lib/ into libqudo-pqc.a (boundary mode, ADR-0003)
#   2. OpenSSL is configured and built from openssl/
#
# Stage 2 configures OpenSSL against stage 1's archive, so the PQC entry points
# in crypto/ml_kem, crypto/ml_dsa and crypto/slh_dsa delegate to qudo-pqc
# (ADR-0005). Static linkage is mandatory: the integrity HMAC must cover the
# algorithm code in one .text region (design 12.3 RULE).

SHELL := /bin/bash
REPO_ROOT := $(abspath $(dir $(lastword $(MAKEFILE_LIST)))..)
OPENSSL_DIR := $(REPO_ROOT)/openssl
QUDO_PQC_DIR := $(REPO_ROOT)/qudo-pqc-lib
QUDO_PQC_BUILD := $(QUDO_PQC_DIR)/build
QUDO_PQC_ARCHIVE := $(QUDO_PQC_BUILD)/lib/libqudo-pqc.a
# Separate build dir for qudo-pqc-lib's OWN standalone test suite. It is a FULL
# (non-math-only) build: those tests exercise qudo's FIPS DRBG/POST/integrity,
# which the math-only archive deliberately omits. Kept apart from QUDO_PQC_BUILD
# so it never touches the math-only archive the FIPS build consumes.
QUDO_PQC_TEST_BUILD := $(QUDO_PQC_DIR)/build-standalone-test

# ---- configuration knobs ----------------------------------------------------
FIPS ?= yes
JOBS ?= $(shell (command -v nproc >/dev/null && nproc) || sysctl -n hw.ncpu || echo 4)
PREFIX ?= $(REPO_ROOT)/build/out

# Reproducible-build inputs (Story 1.7). SOURCE_DATE_EPOCH is fixed rather than
# derived from the clock so two clean builds agree.
SOURCE_DATE_EPOCH ?= 1752451200
export SOURCE_DATE_EPOCH

# ---- platform detection -----------------------------------------------------
UNAME_S := $(shell uname -s)
UNAME_M := $(shell uname -m)

ifeq ($(UNAME_S),Linux)
  ifeq ($(UNAME_M),x86_64)
    OPENSSL_TARGET ?= linux-x86_64
  else ifneq (,$(filter $(UNAME_M),aarch64 arm64))
    OPENSSL_TARGET ?= linux-aarch64
  else
    OPENSSL_TARGET ?= linux-generic64
  endif
  # --build-id=none for reproducibility; -z noexecstack because
  # keccak1600-x86_64.s.o in libqudo-pqc.a lacks a .note.GNU-stack marker, so
  # linkers otherwise mark the whole module's stack RWE. Shipping an
  # executable-stack FIPS module trips SELinux deny_execstack and is a
  # hardening regression a lab will query. OpenSSL uses -Wa,--noexecstack for
  # the same reason on its own assembly.
  REPRO_LDFLAGS ?= -Wl,--build-id=none -Wl,-z,noexecstack
else ifeq ($(UNAME_S),Darwin)
  ifeq ($(UNAME_M),arm64)
    OPENSSL_TARGET ?= darwin64-arm64-cc
  else
    OPENSSL_TARGET ?= darwin64-x86_64-cc
  endif
  # macOS is a confirmed cert OE, so byte-reproducibility here is mandatory.
  #
  # The design (§12.3, §22.1) prescribes -Wl,-no_uuid. That does NOT work:
  # stripping LC_UUID from libcrypto.dylib makes the macOS linker refuse to
  # link anything against it ("ld: missing LC_UUID load command in
  # './libcrypto.dylib'"), breaking engines/, providers/ and fuzz/.
  #
  # Determinism on macOS is achieved instead by:
  #   ZERO_AR_DATE=1  zero the timestamps ar/libtool embed in static archives
  #   LC_UUID         ld64 derives the UUID from a content hash, so identical
  #                   inputs produce an identical UUID -- no stripping needed
  #   -ffile-prefix-map  keep absolute build paths out of the binaries
  REPRO_LDFLAGS ?=
  export ZERO_AR_DATE := 1
else
  OPENSSL_TARGET ?= cc
  REPRO_LDFLAGS ?=
endif

REPRO_CFLAGS ?= -ffile-prefix-map=$(REPO_ROOT)=.

ifeq ($(FIPS),yes)
  FIPS_FLAG := enable-fips
else
  FIPS_FLAG :=
endif

CONFIG_STAMP := $(OPENSSL_DIR)/configdata.pm

.PHONY: all qudo-pqc openssl openssl-config test test-pqc install install_sw clean distclean repro-check help

# Sub-makes rather than prerequisites, so the two stages stay ordered even
# under -j. Stage 2 will consume stage 1's archive once Story 6.1 lands.
all:
	@$(MAKE) --no-print-directory qudo-pqc
	@$(MAKE) --no-print-directory openssl

help:
	@echo "QudoSSL build wrapper"
	@echo "  target platform : $(OPENSSL_TARGET)"
	@echo "  fips            : $(FIPS)"
	@echo "  jobs            : $(JOBS)"
	@echo ""
	@echo "  all           build stage 1 + stage 2"
	@echo "  qudo-pqc      stage 1 only -> libqudo-pqc.a"
	@echo "  openssl       stage 2 only -> libcrypto.so, libssl.so"
	@echo "  test          run the OpenSSL test suite"
	@echo "  test-pqc      run qudo-pqc-lib's own test suite"
	@echo "  repro-check   build twice, compare hashes"
	@echo "  install       install into PREFIX ($(PREFIX))"
	@echo "  clean         remove build outputs"
	@echo "  distclean     clean + remove OpenSSL configuration"

# ---- stage 1 ----------------------------------------------------------------
# Always delegate to CMake; never gate on the archive file. CMake does its own
# up-to-date checking and detects edited qudo-pqc-lib sources. Making
# libqudo-pqc.a a file target skipped the sub-build whenever the archive merely
# existed, which silently linked a stale archive after any source edit.
qudo-pqc:
	@JOBS=$(JOBS) $(REPO_ROOT)/build/build_libqudo_pqc.sh

# ---- stage 2 ----------------------------------------------------------------
openssl-config: check-prefix $(CONFIG_STAMP)

# PREFIX is baked into configdata.pm by Configure. Changing it on a later
# `make` command line does NOT reconfigure, so the build silently keeps
# installing to the OLD prefix -- and any install step that derives paths from
# $(PREFIX) then writes somewhere the artifacts never reach. Detect the
# mismatch and force a reconfigure.
.PHONY: check-prefix
check-prefix:
	@if [ -f "$(CONFIG_STAMP)" ] \
	   && ! grep -q '"prefix" => "$(PREFIX)"' "$(CONFIG_STAMP)" 2>/dev/null; then \
	    echo "==> PREFIX changed to $(PREFIX) -- reconfiguring"; \
	    rm -f "$(CONFIG_STAMP)"; \
	fi


$(CONFIG_STAMP): $(QUDO_PQC_ARCHIVE)
	@echo "==> Stage 2: configuring OpenSSL ($(OPENSSL_TARGET), fips=$(FIPS))"
	cd $(OPENSSL_DIR) && ./Configure $(OPENSSL_TARGET) $(FIPS_FLAG) \
	    --prefix=$(PREFIX) \
	    --libdir=lib \
	    --with-qudo-pqc-archive=$(QUDO_PQC_ARCHIVE) \
	    --with-qudo-pqc-include=$(QUDO_PQC_DIR)/include \
	    --with-qudo-pqc-include=$(QUDO_PQC_DIR)/qudo-mlkem/include \
	    --with-qudo-pqc-include=$(QUDO_PQC_DIR)/qudo-mldsa/include \
	    --with-qudo-pqc-include=$(QUDO_PQC_DIR)/qudo-slhdsa/include \
	    $(REPRO_CFLAGS) \
	    $(REPRO_LDFLAGS) \
	    $(OPENSSL_EXTRA_FLAGS)

openssl: openssl-config
	@echo "==> Stage 2: building OpenSSL"
	$(MAKE) -C $(OPENSSL_DIR) -j$(JOBS)
	@echo "==> Build complete:"
	@ls -1 $(OPENSSL_DIR)/libcrypto.*[ao] $(OPENSSL_DIR)/libcrypto.*dylib \
	       $(OPENSSL_DIR)/libssl.*[ao] $(OPENSSL_DIR)/libssl.*dylib 2>/dev/null || true
# Match only built modules. A bare fips.* glob also catches fips.checksum and
# fips.module.sources, which are tracked upstream sources, not build outputs --
# and fips.module.sources defines the cert boundary, so listing it as a build
# artifact is actively misleading.
	@ls -1 $(OPENSSL_DIR)/providers/fips.so $(OPENSSL_DIR)/providers/fips.dylib \
	       $(OPENSSL_DIR)/providers/fips.dll 2>/dev/null || true

# ---- tests ------------------------------------------------------------------
test: all
	$(MAKE) -C $(OPENSSL_DIR) test HARNESS_JOBS=$(JOBS)

# Runs qudo-pqc-lib's own standalone test suite against its FULL build, not the
# math-only archive. "Standalone" means the complete library including its FIPS
# services; test_pqc_drbg / test_pqc_post / test_pqc_integrity link those, so a
# math-only build cannot even compile them. The math-only archive is verified
# separately by the boundary gates and the FIPS build jobs.
test-pqc:
	cmake -S $(QUDO_PQC_DIR) -B $(QUDO_PQC_TEST_BUILD) \
	    -DCMAKE_BUILD_TYPE=Release \
	    -DBUILD_SHARED_LIBS=OFF \
	    -DQUDO_PQC_BUILD_TESTS=ON
	cmake --build $(QUDO_PQC_TEST_BUILD) --parallel $(JOBS)
	cd $(QUDO_PQC_TEST_BUILD) && ctest --output-on-failure

# ---- reproducibility --------------------------------------------------------
repro-check:
	@$(REPO_ROOT)/reproducible-build/verify-reproducible.sh

# ---- install ----------------------------------------------------------------
# Installs OpenSSL into $(PREFIX). The FIPS module ships as fips.so/.dylib --
# NOT renamed (ADR-0006) -- so it lands at $(PREFIX)/lib/ossl-modules/fips.*
# exactly as upstream would place it.
install: openssl
	$(MAKE) -C $(OPENSSL_DIR) install DESTDIR=$(DESTDIR)
	@$(MAKE) --no-print-directory install-cli-name
	@echo "==> Installed to $(PREFIX)"
	@echo "    Run 'qudossl fipsinstall' to generate fipsmodule.cnf for the module."

install_sw: openssl
	$(MAKE) -C $(OPENSSL_DIR) install_sw DESTDIR=$(DESTDIR)
	@$(MAKE) --no-print-directory install-cli-name

# The product's CLI is `qudossl`, installed as a wrapper next to the upstream
# `openssl` binary (ADR-0013).
#
#   openssl  the real binary, byte-identical behaviour to upstream. Every
#            existing script, Makefile and --with-openssl build keeps working,
#            and `openssl version` output is unchanged so parsers are safe.
#   qudossl  a wrapper that prints the QudoSSL product banner on `version`
#            and delegates everything else unchanged.
#
# A wrapper rather than a renamed binary because the version string comes from
# OPENSSL_VERSION_TEXT in opensslv.h.in, which IS inside the FIPS boundary.
# See the header comment in build/qudossl for why patching apps/version.c was
# rejected. The CLI itself is outside the boundary either way -- apps/ has zero
# entries in providers/fips.module.sources -- so this has no cert impact.
.PHONY: install-cli-name
install-cli-name:
	@bindir="$(DESTDIR)$(PREFIX)/bin"; \
	if [ ! -x "$$bindir/openssl" ]; then \
	    echo "install-cli-name: no openssl at $$bindir -- did PREFIX change without a reconfigure?" >&2; \
	    exit 1; \
	fi; \
	install -m 755 "$(REPO_ROOT)/build/qudossl" "$$bindir/qudossl"; \
	echo "==> CLI: openssl (binary) + qudossl (wrapper) installed in $$bindir"

# ---- housekeeping -----------------------------------------------------------
# ---------------------------------------------------------------------------
# Release artefacts. The tarball is cut from git HEAD — not the working tree —
# so build output and local edits can never reach it, and the version comes
# from the CLI wrapper so there is one place to bump.
# ---------------------------------------------------------------------------
DIST_VERSION := $(shell sed -n 's/^QUDO_PRODUCT_VERSION="\(.*\)"/\1/p' $(REPO_ROOT)/build/qudossl)
DIST_NAME    := qudossl-community-$(DIST_VERSION)
DIST_DIR     := $(REPO_ROOT)/dist
SOURCE_EPOCH := $(shell git -C $(REPO_ROOT) log -1 --format=%ct 2>/dev/null || echo 0)

.PHONY: dist dist-verify

dist:
	@test -n "$(DIST_VERSION)" || \
	  { echo "dist: cannot read QUDO_PRODUCT_VERSION from build/qudossl" >&2; exit 1; }
	@if tag=$$(git -C $(REPO_ROOT) describe --tags --exact-match 2>/dev/null); then \
	  if [ "$$tag" != "$(DIST_NAME)" ]; then \
	    echo "dist: HEAD tag '$$tag' != '$(DIST_NAME)' (from the wrapper's QUDO_PRODUCT_VERSION)" >&2; \
	    exit 1; fi; \
	else \
	  echo "==> note: HEAD is untagged — building a SNAPSHOT (tag $(DIST_NAME) for a release)"; \
	fi
	@echo "==> staging $(DIST_NAME) from git HEAD"
	@rm -rf "$(DIST_DIR)/$(DIST_NAME)" && mkdir -p "$(DIST_DIR)/$(DIST_NAME)"
	git -C $(REPO_ROOT) archive --format=tar HEAD | tar -x -C "$(DIST_DIR)/$(DIST_NAME)"
	@echo "==> normalising timestamps to the HEAD commit date"
	@stamp=$$(perl -e '@t = gmtime($(SOURCE_EPOCH)); \
	  printf "%04d%02d%02d%02d%02d.%02d", $$t[5]+1900, $$t[4]+1, $$t[3], $$t[2], $$t[1], $$t[0]'); \
	TZ=UTC find "$(DIST_DIR)/$(DIST_NAME)" -exec touch -t "$$stamp" {} +
	@echo "==> packing $(DIST_NAME)-src.tar.gz"
	@if tar --version 2>/dev/null | grep -q GNU; then \
	  tar --sort=name --owner=0 --group=0 --numeric-owner \
	      -C "$(DIST_DIR)" -czf "$(DIST_DIR)/$(DIST_NAME)-src.tar.gz" "$(DIST_NAME)"; \
	else \
	  (cd "$(DIST_DIR)" && find "$(DIST_NAME)" | sort | \
	    COPYFILE_DISABLE=1 tar -czf "$(DIST_NAME)-src.tar.gz" --uid 0 --gid 0 -n -T -); \
	fi
	@(cd "$(DIST_DIR)" && shasum -a 256 "$(DIST_NAME)-src.tar.gz" > "$(DIST_NAME)-src.tar.gz.sha256")
	@rm -rf "$(DIST_DIR)/$(DIST_NAME)"
	@echo "==> $(DIST_DIR)/$(DIST_NAME)-src.tar.gz"
	@cat "$(DIST_DIR)/$(DIST_NAME)-src.tar.gz.sha256"

# Build, install and acceptance-check FROM THE TARBALL — the artefact a user
# receives — so a release cannot pass on something only the working tree has.
dist-verify:
	@bash "$(REPO_ROOT)/build/dist-verify.sh" "$(DIST_DIR)/$(DIST_NAME)-src.tar.gz"

clean:
	-$(MAKE) -C $(OPENSSL_DIR) clean 2>/dev/null
	rm -rf $(QUDO_PQC_BUILD)
	rm -rf $(QUDO_PQC_TEST_BUILD)
	rm -rf $(PREFIX)

distclean: clean
	-$(MAKE) -C $(OPENSSL_DIR) distclean 2>/dev/null
	rm -f $(CONFIG_STAMP)
