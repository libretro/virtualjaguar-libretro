<#
tools/jaguar-toolchain/setup.ps1 -- Windows fetch/build/env for the Jaguar
assembler toolchain (rmac, rln, lyxass, pc_jagcrypt, new_bjl/BJL_ROOT).

Mirrors tools/jaguar-toolchain/setup.sh's fetch/build/env subcommands and
reads the SAME PIN file next to this script -- do not duplicate tool
URLs/SHAs here. PIN is the one source of truth for both platforms so a
mixed Linux/macOS/Windows dev team (and CI) stay pinned to the same
commits.

Usage:
    tools\jaguar-toolchain\setup.ps1 -Fetch
    tools\jaguar-toolchain\setup.ps1 -Build
    tools\jaguar-toolchain\setup.ps1 -Env

UNVERIFIED ON WINDOWS: written and syntax/logic-checked with PowerShell 7
(pwsh) on macOS -- there is no Windows host in this session to actually
run -Fetch/-Build against a real MinGW/MSYS2 toolchain, or to confirm the
built rmac/rln/lyxass/pc_jagcrypt binaries work. The per-tool make-variable
overrides in the Build-* functions below are derived from reading each
pinned tool's actual Makefile (see comments inline), not guessed -- but
"derived from source" is not the same as "confirmed by running it." A
Windows contributor or CI runner needs to confirm this before that caveat
can be removed.
#>
param(
    [switch]$Fetch,
    [switch]$Build,
    [switch]$Env
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$RepoRoot = (Resolve-Path (Join-Path $ScriptDir "..\..")).Path
$PinFile = Join-Path $ScriptDir "PIN"

# Matches setup.sh: ${JAGUAR_TOOLCHAIN_DIR:-${REPO_ROOT}/tools/vendor/jaguar-toolchain}
if ($env:JAGUAR_TOOLCHAIN_DIR) {
    $VendorDir = $env:JAGUAR_TOOLCHAIN_DIR
} else {
    $VendorDir = Join-Path $RepoRoot "tools\vendor\jaguar-toolchain"
}

$Tools = @("rmac", "rln", "lyxass", "pc_jagcrypt", "new_bjl")
$BuildTools = @("rmac", "rln", "lyxass", "pc_jagcrypt")

# Invoke-Native: run an external command and (unless -AllowFailure) throw on
# a nonzero exit code. PowerShell does NOT stop on a native command's
# nonzero exit by default -- $ErrorActionPreference = "Stop" alone only
# covers terminating cmdlet errors, not external processes, on both Windows
# PowerShell 5.1 and pwsh 7 without opting into
# $PSNativeCommandUseErrorActionPreference (7.3+, not assumed here). This is
# the equivalent of bash's `set -e` for the git/make calls below.
function Invoke-Native {
    param(
        [Parameter(Mandatory = $true)][string]$Exe,
        [Parameter(Mandatory = $true)][string[]]$CmdArgs,
        [switch]$AllowFailure
    )
    & $Exe @CmdArgs
    if ($LASTEXITCODE -ne 0 -and -not $AllowFailure) {
        throw "$Exe $($CmdArgs -join ' ') exited $LASTEXITCODE"
    }
    return $LASTEXITCODE
}

# Read-Pin <tool> <key> -- e.g. Read-Pin rmac URL. Mirrors setup.sh's awk
# read_pin(): scan PIN for the "[tool]" section, return the first "key="
# line inside it, stop scanning that section at the next "[" header.
function Read-Pin {
    param([string]$Tool, [string]$Key)
    $inSection = $false
    foreach ($line in Get-Content -LiteralPath $PinFile) {
        if ($line -eq "[$Tool]") { $inSection = $true; continue }
        if ($line -match '^\[') { $inSection = $false }
        if ($inSection -and $line -match "^$Key=(.*)") { return $Matches[1] }
    }
    return $null
}

function Invoke-FetchAll {
    New-Item -ItemType Directory -Force -Path $VendorDir | Out-Null
    foreach ($tool in $Tools) {
        $url = Read-Pin $tool "URL"
        $ref = Read-Pin $tool "REF"
        $sha = Read-Pin $tool "SHA"
        $dest = Join-Path $VendorDir $tool
        Write-Host "==> $tool"
        if (-not (Test-Path (Join-Path $dest ".git"))) {
            Write-Host "    cloning $url"
            Invoke-Native git @("clone", "--filter=blob:none", $url, $dest) | Out-Null
        } else {
            Write-Host "    fetching $url"
            Invoke-Native git @("-C", $dest, "remote", "set-url", "origin", $url) -AllowFailure | Out-Null
            Invoke-Native git @("-C", $dest, "fetch", "origin", $ref) | Out-Null
        }
        if ($sha -ne "HEAD") {
            # Partial clone (--filter=blob:none) plus a ref-only fetch may
            # not have the pinned SHA reachable (e.g. it isn't the tip of
            # $ref) -- fetch it explicitly first if checkout would miss.
            # Mirrors setup.sh's cat-file -e probe + fallback fetch.
            $probeExit = Invoke-Native git @("-C", $dest, "cat-file", "-e", "$sha^{commit}") -AllowFailure
            if ($probeExit -ne 0) {
                Invoke-Native git @("-C", $dest, "fetch", "origin", $sha) -AllowFailure | Out-Null
            }
            Invoke-Native git @("-C", $dest, "checkout", "--detach", $sha) | Out-Null
        } else {
            Invoke-Native git @("-C", $dest, "checkout", "--detach", "origin/$ref") | Out-Null
        }
        $short = & git -C $dest rev-parse --short HEAD
        Write-Host "    at $short"
    }
}

# Get-MakeTool: prefer MSYS2/MinGW's mingw32-make (what the toolchain's
# upstream Makefiles are written against and tested with), fall back to a
# plain `make` if that's what's on PATH (e.g. WSL-adjacent or Cygwin setups
# that alias it directly).
function Get-MakeTool {
    if (Get-Command mingw32-make -ErrorAction SilentlyContinue) { return "mingw32-make" }
    if (Get-Command make -ErrorAction SilentlyContinue) { return "make" }
    return $null
}

function Build-Rmac {
    param([string]$Dest, [string]$MakeCmd)
    # rmac/makefile auto-detects MinGW by shelling out to `uname -a` and
    # grepping for "MINGW" to select -std=gnu99 over the default -std=c99
    # (its own comment: "Should catch MinGW" -- needed because rmac.h pulls
    # in GNU/MinGW-specific definitions under __MINGW32__). That autodetect
    # only works when `uname` is on PATH, which holds inside an MSYS2 shell
    # but is not guaranteed when mingw32-make is invoked directly from a
    # plain PowerShell prompt (no MSYS2 utils on PATH). STD is assigned with
    # `:=` in the makefile, so a make command-line override still wins over
    # it -- force it explicitly instead of hoping `uname` resolves.
    Invoke-Native $MakeCmd @("-C", $Dest, "-j1", "STD=gnu99") | Out-Null
}

function Build-Rln {
    param([string]$Dest, [string]$MakeCmd)
    # rln/makefile picks SYSTYPE=WIN32 (vs the default __GCCUNIX__) by
    # shelling out to `uname -o` and grepping for "Msys" -- same
    # MSYS2-shell-only assumption as rmac above. This one is a correctness
    # issue, not just a build-flag nicety: rln.h has genuinely different
    # code paths gated on `#if defined(WIN32)` vs `#if defined(__GCCUNIX__)`
    # (rln.h lines 10, 30, 49, 52 in the pinned source), so silently
    # building the Unix branch on Windows would produce a binary with the
    # wrong file-handling assumptions rather than just failing loudly.
    # Force SYSTYPE=WIN32 explicitly rather than relying on the uname probe.
    Invoke-Native $MakeCmd @("-C", $Dest, "-j1", "SYSTYPE=WIN32") | Out-Null
}

function Build-Lyxass {
    param([string]$Dest, [string]$MakeCmd)
    # lyxass/Makefile's $(OSTYPE) branches only gate a macOS-only linker
    # flag and the `install` target (which copies into bin/$(OSTYPE) -- we
    # never invoke `install`, only the default `all: lyxass` target, same
    # as setup.sh). No override needed on Windows.
    Invoke-Native $MakeCmd @("-C", $Dest, "-j1") | Out-Null
}

function Build-PcJagcrypt {
    param([string]$Dest, [string]$MakeCmd)
    # pc_jagcrypt's Makefile defaults to SYSTYPE=__GCCUNIX__ and documents
    # a Windows build as a hand-edit: "Uncomment the following line to
    # compile for Win32" (SYSTYPE = __GCCWIN32__). Left at the default on
    # Windows, WHICH stays `which` and the UPX-not-found fallback stays
    # plain `ls` -- neither is a native Windows/cmd command, so the
    # makefile's final recipe line ($(UPX) jagcrypt$(EXESUFFIX)) would fail
    # the build right after a successful link+strip. That's the same
    # "everything worked except the last recipe line" shape as setup.sh's
    # macOS strip-flag workaround (build_pc_jagcrypt), but a different root
    # cause -- and unlike on macOS, MinGW-w64's `strip` is GNU strip and
    # supports --strip-all natively, so setup.sh's strip-shim problem does
    # not reproduce on Windows: there is nothing to shim here, only the
    # SYSTYPE toggle to set. Setting SYSTYPE=__GCCWIN32__ selects
    # EXESUFFIX=.exe, WHICH=where (a native Windows command), and a
    # native-command UPX no-op fallback (dir /B) instead of `ls`.
    Invoke-Native $MakeCmd @("-C", $Dest, "-j1", "SYSTYPE=__GCCWIN32__") | Out-Null
}

# Build-One <tool> <dest> <makeCmd> -- per-tool build dispatch, mirrors
# setup.sh's build_one().
function Build-One {
    param([string]$Tool, [string]$Dest, [string]$MakeCmd)
    switch ($Tool) {
        "rmac" { Build-Rmac -Dest $Dest -MakeCmd $MakeCmd }
        "rln" { Build-Rln -Dest $Dest -MakeCmd $MakeCmd }
        "lyxass" { Build-Lyxass -Dest $Dest -MakeCmd $MakeCmd }
        "pc_jagcrypt" { Build-PcJagcrypt -Dest $Dest -MakeCmd $MakeCmd }
        default { throw "Build-One: unknown tool '$Tool'" }
    }
}

function Invoke-BuildAll {
    $makeCmd = Get-MakeTool
    if (-not $makeCmd) {
        # -ErrorAction Continue: without it, $ErrorActionPreference = "Stop"
        # (set above) turns Write-Error into a terminating error and the
        # `exit 2` below is never reached -- the process would instead exit
        # 1 via PowerShell's own unhandled-error path, masking the "not
        # fetched / no make" case setup.sh signals distinctly with exit 2.
        Write-Error "no make/mingw32-make found on PATH -- install MSYS2/MinGW (or Cygwin) and add its bin dir to PATH, or build each tool's Vs2015 project manually" -ErrorAction Continue
        exit 2
    }
    $failCount = 0
    foreach ($tool in $BuildTools) {
        $dest = Join-Path $VendorDir $tool
        if (-not (Test-Path $dest)) {
            Write-Error "$tool not fetched -- run -Fetch first" -ErrorAction Continue
            exit 2
        }
        Write-Host "==> building $tool"
        try {
            Build-One -Tool $tool -Dest $dest -MakeCmd $makeCmd
        } catch {
            Write-Warning "build failed for $tool -- $($_.Exception.Message)"
            $failCount++
        }
    }
    # new_bjl ships prebuilt binaries under bin/ -- nothing to build.
    if ($failCount -gt 0) {
        Write-Host "==> $failCount tool(s) failed to build"
        exit 1
    }
    Write-Host "==> all tools built"
}

function Invoke-EnvPrint {
    $bjlRoot = Join-Path $VendorDir "new_bjl"
    $toolPaths = $BuildTools | ForEach-Object { Join-Path $VendorDir $_ }
    Write-Output "`$env:PATH = `"$($toolPaths -join ';');`$env:PATH`""
    Write-Output "`$env:BJL_ROOT = `"$bjlRoot`""
}

if ($Fetch) { Invoke-FetchAll }
elseif ($Build) { Invoke-BuildAll }
elseif ($Env) { Invoke-EnvPrint }
else {
    Write-Host "usage: setup.ps1 -Fetch | -Build | -Env"
    exit 2
}
