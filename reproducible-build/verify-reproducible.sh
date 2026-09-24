#!/usr/bin/env bash
#
# Build QudoSSL twice from a clean tree and compare artifact hashes.
#
# Sprint 1 covers libcrypto / libssl / libqudo-pqc.a. The FIPS module joins the
# list once Story 6.1 links the archive into it (Story 6.7 owns the full check).
#
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
WORK="${REPRO_WORKDIR:-$(mktemp -d)}"
KEEP="${REPRO_KEEP:-0}"

ARTIFACTS=(
    "qudo-pqc-lib/build/lib/libqudo-pqc.a"
    "openssl/libcrypto.a"
    "openssl/libssl.a"
)

# The FIPS module is the cert artifact, so it must be covered. Its extension is
# platform-dependent and it only exists in a FIPS build, hence the glob.
case "$(uname -s)" in
    Darwin) ARTIFACTS+=("openssl/providers/fips.dylib") ;;
    Linux)  ARTIFACTS+=("openssl/providers/fips.so") ;;
esac

hash_artifacts() {
    local root="$1" out="$2"
    : > "${out}"
    for rel in "${ARTIFACTS[@]}"; do
        if [ -f "${root}/${rel}" ]; then
            if command -v sha256sum >/dev/null; then
                printf '%s  %s\n' "$(sha256sum "${root}/${rel}" | cut -d' ' -f1)" "${rel}" >> "${out}"
            else
                printf '%s  %s\n' "$(shasum -a 256 "${root}/${rel}" | cut -d' ' -f1)" "${rel}" >> "${out}"
            fi
        else
            # Fail closed. Writing a MISSING marker made both sides agree when
            # every artifact was absent, so the check reported PASS on a build
            # that produced nothing.
            echo "error: expected artifact not produced: ${root}/${rel}" >&2
            return 1
        fi
    done
}

# Both builds MUST happen at the same absolute path.
#
# build/Makefile derives -ffile-prefix-map=$(REPO_ROOT)=. from the checkout
# location, and OpenSSL bakes its literal compiler-flag string into
# crypto/buildinf.h, which ends up inside libcrypto. Building in .../a and
# .../b therefore embeds two different flag strings and the comparison fails on
# a difference the harness itself created. Use one path, twice.
BUILD_PATH="${WORK}/src"

build_once() {
    local label="$1"
    echo "==> clean checkout -> ${BUILD_PATH} (build ${label})"
    rm -rf "${BUILD_PATH}"
    git -C "${REPO_ROOT}" worktree prune >/dev/null 2>&1 || true
    git -C "${REPO_ROOT}" worktree add --detach "${BUILD_PATH}" HEAD >/dev/null 2>&1 \
        || { mkdir -p "${BUILD_PATH}"; tar -C "${REPO_ROOT}" --exclude=.git -cf - . | tar -C "${BUILD_PATH}" -xf -; }
    make -C "${BUILD_PATH}/build" all
}

echo "workdir: ${WORK}"

build_once A
hash_artifacts "${BUILD_PATH}" "${WORK}/a.sha" || { echo "FAIL: build A did not produce all artifacts" >&2; exit 1; }

build_once B
hash_artifacts "${BUILD_PATH}" "${WORK}/b.sha" || { echo "FAIL: build B did not produce all artifacts" >&2; exit 1; }

echo
echo "=== build A ==="; cat "${WORK}/a.sha"
echo "=== build B ==="; cat "${WORK}/b.sha"
echo

if diff -u "${WORK}/a.sha" "${WORK}/b.sha"; then
    echo "PASS: builds are byte-identical"
    rc=0
else
    echo "FAIL: builds differ -- see the diff above" >&2
    rc=1
fi

if [ "${KEEP}" != "1" ]; then
    git -C "${REPO_ROOT}" worktree remove --force "${BUILD_PATH}" 2>/dev/null || true
    git -C "${REPO_ROOT}" worktree prune >/dev/null 2>&1 || true
    rm -rf "${WORK}"
fi
exit ${rc}
