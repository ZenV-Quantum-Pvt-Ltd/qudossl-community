<#
.SYNOPSIS
    QudoSSL Windows build wrapper (MSVC / nmake).

.DESCRIPTION
    Windows equivalent of build/Makefile. Same two stages:
      1. CMake builds qudo-pqc-lib into a static archive (boundary mode).
      2. OpenSSL is configured for VC-WIN64A and built with nmake.

    Windows x64 is a confirmed cert OE, so this path is cert-relevant, not a
    convenience. Run from a "x64 Native Tools Command Prompt for VS 2022", or
    any shell where cl.exe and nmake.exe are on PATH.

.PARAMETER Target
    all | qudo-pqc | openssl | test | clean

.PARAMETER Fips
    Build with enable-fips. Default true.

.EXAMPLE
    pwsh -File build\build.ps1 -Target all
#>
[CmdletBinding()]
param(
    [ValidateSet('all', 'qudo-pqc', 'openssl', 'test', 'clean')]
    [string]$Target = 'all',
    [bool]$Fips = $true
)

$ErrorActionPreference = 'Stop'

$RepoRoot     = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$OpenSslDir   = Join-Path $RepoRoot 'openssl'
$QudoPqcDir   = Join-Path $RepoRoot 'qudo-pqc-lib'
$QudoPqcBuild = Join-Path $QudoPqcDir 'build'
$Prefix       = Join-Path $RepoRoot 'build\out'

# Fixed epoch so two clean builds agree (see build/Makefile).
if (-not $env:SOURCE_DATE_EPOCH) { $env:SOURCE_DATE_EPOCH = '1752451200' }

function Assert-Tool([string]$Name, [string]$Hint) {
    if (-not (Get-Command $Name -ErrorAction SilentlyContinue)) {
        throw "Required tool '$Name' not found on PATH. $Hint"
    }
}

function Invoke-Checked([string]$Exe, [string[]]$Arguments, [string]$WorkDir) {
    Push-Location $WorkDir
    try {
        Write-Host "+ $Exe $($Arguments -join ' ')"
        # Merge stderr into stdout and stream it, so a failing MSVC/CMake build
        # shows its actual diagnostics in the CI log rather than only this
        # wrapper's exit-code message.
        & $Exe @Arguments 2>&1 | ForEach-Object { Write-Host $_ }
        if ($LASTEXITCODE -ne 0) {
            throw "$Exe $($Arguments -join ' ') failed with exit code $LASTEXITCODE"
        }
    } finally {
        Pop-Location
    }
}

function Find-QudoArchive {
    # NMake Makefiles is single-config, so ARCHIVE_OUTPUT_DIRECTORY resolves to
    # <build>/lib directly. The Release\ variants are kept as a fallback in case
    # a multi-config generator is ever used.
    $candidates = @(
        (Join-Path $QudoPqcBuild 'lib\qudo-pqc.lib'),
        (Join-Path $QudoPqcBuild 'lib\Release\qudo-pqc.lib'),
        (Join-Path $QudoPqcBuild 'Release\qudo-pqc.lib')
    )
    foreach ($c in $candidates) { if (Test-Path $c) { return $c } }
    $found = Get-ChildItem -Path $QudoPqcBuild -Recurse -Filter 'qudo-pqc*.lib' -ErrorAction SilentlyContinue |
             Select-Object -First 1
    if ($found) { return $found.FullName }
    return $null
}

function Build-QudoPqc {
    Write-Host '==> Stage 1: qudo-pqc-lib -> qudo-pqc.lib'
    Assert-Tool 'cmake' 'Install CMake >= 3.15.'

    # STANDARD build -- qudo-pqc-lib supplies algorithm math only. OpenSSL owns
    # POST, CAST, PCT, integrity and the approved DRBG under crypto-layer
    # delegation. No -DQUDO_FIPS_MODULE. See ADR-0009 and build_libqudo_pqc.sh.
    # Generator MUST be "NMake Makefiles" -- that is the only Windows
    # configuration qudo-pqc-lib supports and tests (see its build_windows.bat,
    # which sets GENERATOR=NMake Makefiles for every MSVC path). The multi-config
    # "Visual Studio 17 2022" generator is not exercised upstream and fails at
    # build time. Requires an MSVC environment on PATH, which the CI job sets up
    # via msvc-dev-cmd and a developer gets from a x64 Native Tools prompt.
    #
    # Fail closed on a stale subtree, exactly as build_libqudo_pqc.sh does.
    # QUDO_PQC_MATH_ONLY is what keeps qudo-pqc's own FIPS infrastructure out
    # of the archive; without it the boundary gate has something real to catch.
    $cml = Join-Path $QudoPqcDir 'CMakeLists.txt'
    if (-not (Select-String -Path $cml -Pattern 'QUDO_PQC_MATH_ONLY' -Quiet)) {
        throw @"
the qudo-pqc-lib subtree does not support QUDO_PQC_MATH_ONLY.
Re-pin the subtree to a revision that has it (see ADR-0009).
"@
    }

    $cmakeArgs = @(
        '-S', $QudoPqcDir,
        '-B', $QudoPqcBuild,
        '-G', 'NMake Makefiles',
        '-DCMAKE_BUILD_TYPE=Release',
        '-DBUILD_SHARED_LIBS=OFF',
        '-DQUDO_PQC_MATH_ONLY=ON',
        '-DQUDO_PQC_BUILD_TESTS=OFF'
    )
    Invoke-Checked 'cmake' $cmakeArgs $RepoRoot
    # Single-config generator: build type came from CMAKE_BUILD_TYPE above, so
    # no --config here.
    #
    # Build ONLY the qudo-pqc target -- the static qudo-pqc.lib that QudoSSL
    # links. The standalone qudo-mlkem/qudo-mldsa/qudo-slhdsa sub-libraries
    # recompile the wrapper sources without the static-link linkage macros and
    # fail on MSVC with C2491 (definition of dllimport function not allowed).
    # QudoSSL never consumes them, so do not build them.
    Invoke-Checked 'cmake' @('--build', $QudoPqcBuild, '--target', 'qudo-pqc') $RepoRoot

    $archive = Find-QudoArchive
    if (-not $archive) { throw "Stage 1 produced no qudo-pqc archive under $QudoPqcBuild" }
    Write-Host "==> Stage 1 complete: $archive"
    return $archive
}

function Build-OpenSsl {
    Write-Host '==> Stage 2: configuring OpenSSL (VC-WIN64A)'
    Assert-Tool 'perl' 'Install Strawberry Perl and put perl.exe on PATH.'
    Assert-Tool 'nmake' 'Run from a x64 Native Tools Command Prompt for VS 2022.'

    $cfg = @('Configure', 'VC-WIN64A', "--prefix=$Prefix")
    if ($Fips) { $cfg += 'enable-fips' }

    # Crypto-layer delegation (ADR-0005). These are NOT optional: without them
    # Configure never defines QUDO_PQC_DELEGATE, so the build silently compiles
    # upstream OpenSSL's own ML-KEM/ML-DSA math and the resulting binaries are
    # not QudoSSL at all -- a green Windows job that validated the wrong
    # product. Mirrors build/Makefile lines 127-134.
    $archive = Find-QudoArchive
    if (-not $archive) {
        throw "no qudo-pqc archive under $QudoPqcBuild -- run stage 1 first (-Target all)."
    }
    $cfg += "--with-qudo-pqc-archive=$archive"
    foreach ($inc in @('include',
                       'qudo-mlkem\include',
                       'qudo-mldsa\include',
                       'qudo-slhdsa\include')) {
        $cfg += "--with-qudo-pqc-include=$(Join-Path $QudoPqcDir $inc)"
    }

    # /Brepro asks the MSVC toolchain for a deterministic PE (no timestamp).
    # NOTE: Windows reproducibility is NOT yet verified end to end -- see
    # docs/reproducibility.md. Treat this as a starting point, not a guarantee.
    $cfg += '/Brepro'

    Invoke-Checked 'perl' $cfg $OpenSslDir

    Write-Host '==> Stage 2: building OpenSSL'
    Invoke-Checked 'nmake' @() $OpenSslDir

    Write-Host '==> Build complete:'
    Get-ChildItem -Path $OpenSslDir -Filter 'libcrypto*' -ErrorAction SilentlyContinue |
        ForEach-Object { Write-Host "    $($_.Name)" }
    Get-ChildItem -Path (Join-Path $OpenSslDir 'providers') -Filter 'fips.dll' -ErrorAction SilentlyContinue |
        ForEach-Object { Write-Host "    providers\$($_.Name)" }
}

switch ($Target) {
    'qudo-pqc' { Build-QudoPqc | Out-Null }
    'openssl'  { Build-OpenSsl }
    'all'      { Build-QudoPqc | Out-Null; Build-OpenSsl }
    'test'     {
        Build-QudoPqc | Out-Null
        Build-OpenSsl
        Invoke-Checked 'nmake' @('test') $OpenSslDir
    }
    'clean'    {
        if (Test-Path $QudoPqcBuild) { Remove-Item -Recurse -Force $QudoPqcBuild }
        if (Test-Path $Prefix)       { Remove-Item -Recurse -Force $Prefix }
        if (Test-Path (Join-Path $OpenSslDir 'makefile')) {
            Invoke-Checked 'nmake' @('clean') $OpenSslDir
        }
    }
}
