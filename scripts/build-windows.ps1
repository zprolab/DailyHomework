<#
    Windows build script (MSVC + Qt 6).

    Usage:
        powershell -ExecutionPolicy Bypass -File scripts\build-windows.ps1 `
            -QtDir C:\Qt\6.12.0\msvc2022_64 -Deploy

    Artifacts:
        build\apps\qt\Release\DaHoWork.exe
        build\tools\Release\dhw_render_cli.exe
        build\tools\Release\dhw_make_icon.exe

    Two ways to ship:
      1) Dynamic Qt (default): -Deploy runs windeployqt so the whole folder can be
         copied to a classroom machine. Add -Zip to also produce
         dist\DaHoWork-win64.zip.
      2) Single self-contained exe: build against a static Qt (configure -static
         -static-runtime) and pass -StaticQt. The script rejects a shared Qt kit and
         checks the import table of the built exes with dumpbin afterwards. See
         docs/BUILD-WINDOWS.md.

    -Force continues to -Deploy/-Zip even when tests fail (the script still ends with a
    non-zero exit code so the failure stays visible).

    -Fluent builds the WinUI3/Fluent shell (vendored Fluent-Qt, -DDHW_UI_FLUENT=ON).
    Without it the native Qt style shell is built (-DDHW_UI_FLUENT=OFF).

    -Jobs sets the parallel compile job count (default: CPU count); it is passed
    to cmake --build --parallel so the log shows the real -j value.
#>
param(
    [string]$QtDir = "",
    [string]$BuildDir = "build",
    [string]$Config = "Release",
    [int]$Jobs = 0,
    [string]$Generator = "",
    [switch]$StaticQt,
    [switch]$SkipTests,
    [switch]$Deploy,
    [switch]$Zip,
    [switch]$Force,
    [switch]$Fluent,
    [switch]$NoSccache
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
Set-Location $root

if ($Jobs -le 0) {
    $Jobs = [Environment]::ProcessorCount
}

# Reuse object files through sccache when it is on PATH (CI installs it and sets
# SCCACHE_GHA_ENABLED; the CMake compiler launcher makes cl.exe go through it).
$sccache = $null
if (-not $NoSccache) {
    $sccacheCommand = Get-Command sccache.exe -ErrorAction SilentlyContinue
    if (-not $sccacheCommand) {
        $sccacheCommand = Get-Command sccache -ErrorAction SilentlyContinue
    }
    if ($sccacheCommand) {
        $sccache = $sccacheCommand.Source
    }
}

if ($Zip -and -not $Deploy) {
    throw "-Zip requires -Deploy: the archive is made from the deployed folder."
}

if (-not (Get-Command cmake -ErrorAction SilentlyContinue)) {
    throw "cmake is not on PATH. Install CMake (winget install Kitware.CMake) and reopen the shell."
}

# Picking the generator needs two facts: what CMake supports and what Visual Studio is
# actually installed. A CMake older than the installed Visual Studio lists only old
# generators, which then fail with "Could not find any instance of Visual Studio".
function Get-CMakeVisualStudioGenerators {
    $result = @()
    $help = & cmake --help 2>$null
    foreach ($line in $help) {
        if ($line -match '^\s+(Visual Studio (\d+) (\d{4}))\s') {
            $result += [pscustomobject]@{ Name = $matches[1]; Major = [int]$matches[2]; Year = [int]$matches[3] }
        }
    }
    return $result
}

function Get-InstalledVisualStudioMajors {
    $majors = @()
    $vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
    if (-not (Test-Path $vswhere)) { return $majors }
    try {
        $instances = & $vswhere -all -prerelease -format json 2>$null | ConvertFrom-Json
        foreach ($instance in $instances) {
            $major = [int](($instance.installationVersion -split '\.')[0])
            if (-not ($majors -contains $major)) { $majors += $major }
        }
    } catch {
        # vswhere is best effort; the caller falls back to whatever CMake offers.
    }
    return $majors
}

# Whether Qt is linked statically is a property of the Qt kit, not of a CMake flag:
# a Qt configured with -static writes "static" into <prefix>\mkspecs\qconfig.pri.
# -StaticQt only switches our own MSVC runtime to /MT so that the app and Qt agree;
# it cannot make a shared kit self-contained.
function Get-QtKitLinkMode([string]$prefix) {
    $qconfig = Join-Path $prefix "mkspecs\qconfig.pri"
    if (-not (Test-Path $qconfig)) { return "unknown" }
    $line = Select-String -Path $qconfig -Pattern '^QT_CONFIG\s*\+=' | Select-Object -First 1
    if ($null -eq $line) { return "unknown" }
    $tokens = ($line.Line -replace '^QT_CONFIG\s*\+=\s*', '') -split '\s+'
    if ($tokens -contains "static") { return "static" }
    if ($tokens -contains "shared") { return "shared" }
    return "unknown"
}

# dumpbin is not guaranteed to be on PATH outside a Visual Studio developer shell,
# so fall back to the newest MSVC toolset that vswhere reports.
function Find-Dumpbin {
    $command = Get-Command dumpbin.exe -ErrorAction SilentlyContinue
    if ($command) { return $command.Source }
    $vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
    if (-not (Test-Path $vswhere)) { return $null }
    try {
        $instances = & $vswhere -all -prerelease -format json 2>$null | ConvertFrom-Json
    } catch {
        return $null
    }
    foreach ($instance in $instances) {
        $toolsetRoot = Join-Path $instance.installationPath "VC\Tools\MSVC"
        if (-not (Test-Path $toolsetRoot)) { continue }
        foreach ($toolset in @(Get-ChildItem $toolsetRoot -Directory | Sort-Object Name -Descending)) {
            foreach ($hostDir in @("Hostx64\x64", "Hostx86\x64")) {
                $candidate = Join-Path $toolset.FullName "bin\$hostDir\dumpbin.exe"
                if (Test-Path $candidate) { return $candidate }
            }
        }
    }
    return $null
}

function Get-PeDependencies([string]$exePath) {
    $dumpbin = Find-Dumpbin
    if (-not $dumpbin) { return $null }
    $output = & $dumpbin /nologo /dependents $exePath 2>&1 | Out-String
    $names = @()
    foreach ($line in ($output -split "`r?`n")) {
        if ($line -match '^\s+([A-Za-z0-9_.\-]+\.dll)\s*$') { $names += $matches[1] }
    }
    if ($names.Count -eq 0) { return $null }
    return $names
}

# A genuinely static build imports only Windows system DLLs. Qt6*.dll means the Qt
# kit is still shared; VCRUNTIME/MSVCP/ucrtbase/api-ms-win-crt means the MSVC runtime
# is still dynamic, which contradicts -static-runtime and -DDHW_STATIC_QT.
function Assert-StaticExecutable([string]$exePath, [string]$qtPrefix) {
    $deps = Get-PeDependencies $exePath
    if ($null -eq $deps) {
        throw ("could not read the import table of '$exePath' (dumpbin.exe was not found or its " +
               "output could not be parsed). Run this script from a Visual Studio developer shell, " +
               "or install the MSVC C++ toolset.")
    }
    Write-Host ("imports: " + ($deps -join ", "))
    # Any Qt DLL or dynamic MSVC/UCRT DLL means the exe is not self-contained.
    $forbidden = @($deps | Where-Object {
        $_ -match '^(Qt6|VCRUNTIME140|MSVCP140|CONCRT140|ucrtbase|api-ms-win-crt-)'
    })
    if ($forbidden.Count -gt 0) {
        throw ("'$exePath' is NOT self-contained: it still imports " + ($forbidden -join ", ") +
               ". -QtDir ('$qtPrefix') must point at a Qt built with 'configure -static " +
               "-static-runtime' and -StaticQt must be passed (see docs\BUILD-WINDOWS.md).")
    }
}

$cmakeVersion = (cmake --version | Select-Object -First 1)
$vsGenerators = Get-CMakeVisualStudioGenerators
$installedMajors = Get-InstalledVisualStudioMajors

$generatorName = $Generator
if ([string]::IsNullOrEmpty($generatorName)) {
    $candidates = @()
    if ($installedMajors.Count -gt 0) {
        $candidates = @($vsGenerators | Where-Object { $installedMajors -contains $_.Major })
    }
    if ($candidates.Count -eq 0) {
        $candidates = @($vsGenerators)
    }
    if ($candidates.Count -gt 0) {
        $generatorName = ($candidates | Sort-Object -Property Major, Year -Descending | Select-Object -First 1).Name
    }
}

if ([string]::IsNullOrEmpty($generatorName)) {
    # CMake (old or pre-release) does not advertise a generator for the installed
    # Visual Studio. Fall back to a generator that needs no VS mapping; the shell must
    # already provide the MSVC environment (Developer Command/PowerShell for VS).
    if (Get-Command ninja -ErrorAction SilentlyContinue) {
        $generatorName = "Ninja"
        Write-Warning ("$cmakeVersion has no generator for the installed Visual Studio; " +
                       "falling back to Ninja (MSVC environment must be active).")
    } elseif (Get-Command nmake -ErrorAction SilentlyContinue) {
        $generatorName = "NMake Makefiles"
        Write-Warning ("$cmakeVersion has no generator for the installed Visual Studio and " +
                       "ninja is not on PATH; falling back to NMake Makefiles.")
    } else {
        $offered = if ($vsGenerators.Count -gt 0) { ($vsGenerators | ForEach-Object { $_.Name }) -join ", " } else { "none" }
        $installed = if ($installedMajors.Count -gt 0) { ($installedMajors | ForEach-Object { "v$_" }) -join ", " } else { "not detected" }
        throw ("No usable generator.`n" +
               "  $cmakeVersion offers: $offered`n" +
               "  Visual Studio found:  $installed`n" +
               "Fix one of:`n" +
               "  1) Upgrade CMake to a stable release that lists your Visual Studio version`n" +
               "  2) Open the 'x64 Native Tools Command Prompt/PowerShell for VS' (it provides`n" +
               "     cl.exe, rc.exe, ninja or nmake) and rerun this script.")
    }
}

$isVisualStudio = $generatorName -like "Visual Studio*"
if (-not $isVisualStudio) {
    Write-Warning ("generator '$generatorName' is not a Visual Studio generator; " +
                   "run this script from a shell where the MSVC environment is active (vcvars64.bat).")
}
# Visual Studio generators are multi-config (Release/Debug subfolders); Ninja and
# Makefiles are single-config (artifacts directly in the build tree).
$multiConfig = $isVisualStudio
if ($multiConfig) {
    $appDir = Join-Path $BuildDir "apps\qt\$Config"
    $toolsDir = Join-Path $BuildDir "tools\$Config"
} else {
    $appDir = Join-Path $BuildDir "apps\qt"
    $toolsDir = Join-Path $BuildDir "tools"
}
if ($isVisualStudio -and $installedMajors.Count -gt 0) {
    $chosenMajor = ($vsGenerators | Where-Object { $_.Name -eq $generatorName } | Select-Object -First 1).Major
    if ($installedMajors -notcontains $chosenMajor) {
        Write-Warning ("CMake selected '$generatorName' but the installed Visual Studio is v" +
                       (($installedMajors) -join ", v") + "; CMake will likely fail with " +
                       "'Could not find any instance of Visual Studio'. Upgrade CMake or use -Generator Ninja.")
    }
}

if ([string]::IsNullOrEmpty($QtDir)) {
    $candidates = @(
        "C:\Qt\6.12.0\msvc2022_64",
        "C:\Qt\6.11.2\msvc2022_64",
        "C:\Qt\6.9.3\msvc2022_64",
        "C:\Qt\6.8.0\msvc2022_64",
        "C:\Qt\6.8.0\msvc2019_64"
    ) | Where-Object { Test-Path $_ }
    if ($candidates.Count -gt 0) {
        $QtDir = $candidates[0]
    } else {
        throw "Qt6 not found. Pass -QtDir, e.g. -QtDir C:\Qt\6.12.0\msvc2022_64"
    }
}

Write-Host "== toolchain =="
Write-Host $cmakeVersion
Write-Host ("visual studio installed: {0}" -f $(if ($installedMajors.Count -gt 0) { ($installedMajors | ForEach-Object { "v$_" }) -join ", " } else { "not detected" }))
Write-Host "Qt: $QtDir"
Write-Host ("generator: {0}{1}" -f $generatorName, $(if ($isVisualStudio) { " (x64)" } else { "" }))
Write-Host "build dir: $BuildDir ($Config)"
Write-Host ("parallel jobs: {0} (cmake --build --parallel {0})" -f $Jobs)
Write-Host ("sccache: {0}" -f $(if ($sccache) { $sccache } else { "not used" }))
Write-Host ("options: Deploy={0} Zip={1} StaticQt={2} Force={3} SkipTests={4} Fluent={5}" -f $Deploy, $Zip, $StaticQt, $Force, $SkipTests, $Fluent)

$qtLinkMode = Get-QtKitLinkMode $QtDir
Write-Host ("Qt kit link mode: {0}" -f $qtLinkMode)

if ($StaticQt) {
    if (Test-Path (Join-Path $QtDir "bin\Qt6Core.dll")) {
        throw ("-StaticQt was requested but '$QtDir' is a shared Qt kit (bin\Qt6Core.dll exists). " +
               "A shared kit can never make the exe self-contained: build without -StaticQt and use " +
               "-Deploy (windeployqt), or point -QtDir at a static Qt build.")
    }
    if ($qtLinkMode -eq "shared") {
        throw ("-StaticQt was requested but '$QtDir\mkspecs\qconfig.pri' reports a shared Qt build. " +
               "Point -QtDir at the install prefix of a Qt built with 'configure -static " +
               "-static-runtime' (see docs\BUILD-WINDOWS.md, section 'Static Qt').")
    }
    if ($qtLinkMode -ne "static") {
        Write-Warning ("could not confirm that '$QtDir' is a static Qt build (no 'static' token in " +
                       "mkspecs\qconfig.pri); the dumpbin import check after the build will decide.")
    }
} elseif ($qtLinkMode -eq "static") {
    Write-Warning ("'$QtDir' looks like a static Qt build. If it was configured with -static-runtime " +
                   "(the setup docs\BUILD-WINDOWS.md describes), pass -StaticQt as well: without it " +
                   "the app links with the dynamic MSVC runtime (/MD) while Qt uses /MT and the link " +
                   "fails with LNK2038 'RuntimeLibrary' mismatch. Ignore this if Qt was built with " +
                   "the dynamic runtime.")
}

$icon = Join-Path $root "assets\app.ico"
if (-not (Test-Path $icon)) {
    Write-Warning "assets\app.ico is missing, so DaHoWork.exe will have no icon."
    Write-Warning "Generate it first (after one build) and rerun this script:"
    Write-Warning "  cmake --build $BuildDir --config $Config --target dhw_make_icon"
    Write-Warning "  $(Join-Path $toolsDir 'dhw_make_icon.exe') --out assets\app.ico --png assets\app-icon-256.png"
    Write-Warning "The icon tool is offline; nothing is downloaded."
}

$configArgs = @(
    "-S", ".", "-B", $BuildDir, "-G", $generatorName,
    "-DCMAKE_PREFIX_PATH=$QtDir",
    "-DCMAKE_BUILD_TYPE=$Config"
)
if ($isVisualStudio) {
    $configArgs += @("-A", "x64")
}
if ($sccache) {
    $configArgs += "-DCMAKE_C_COMPILER_LAUNCHER=$sccache"
    $configArgs += "-DCMAKE_CXX_COMPILER_LAUNCHER=$sccache"
}
if ($StaticQt) {
    $configArgs += "-DDHW_STATIC_QT=ON"
}
if ($Fluent) {
    $configArgs += "-DDHW_UI_FLUENT=ON"
} else {
    $configArgs += "-DDHW_UI_FLUENT=OFF"
}

cmake @configArgs
if ($LASTEXITCODE -ne 0) { throw "cmake configure failed with exit code $LASTEXITCODE" }

cmake --build $BuildDir --config $Config --parallel $Jobs
if ($LASTEXITCODE -ne 0) { throw "build failed with exit code $LASTEXITCODE" }

$testFailed = $false
if (-not $SkipTests) {
    ctest --test-dir $BuildDir -C $Config --output-on-failure
    if ($LASTEXITCODE -ne 0) {
        if (-not $Force) {
            throw "ctest failed with exit code $LASTEXITCODE (pass -Force to deploy anyway)"
        }
        $testFailed = $true
        Write-Warning "ctest failed with exit code $LASTEXITCODE; -Force is set, continuing to deploy."
    }
}

$exe = Join-Path $appDir "DaHoWork.exe"
if (-not (Test-Path $exe)) { throw "missing $exe (build dir $BuildDir, generator $generatorName)" }

if ($StaticQt) {
    Write-Host ""
    Write-Host "== static self-containment check =="
    Assert-StaticExecutable $exe $QtDir
    $cli = Join-Path $toolsDir "dhw_render_cli.exe"
    if (Test-Path $cli) {
        Assert-StaticExecutable $cli $QtDir
    }
    Write-Host "ok: no Qt or MSVC runtime DLL imports"
}

if ($Deploy) {
    if ($StaticQt) {
        Write-Host "Static Qt build: no windeployqt needed ($exe imports only system DLLs)."
    } else {
        $windeploy = Join-Path $QtDir "bin\windeployqt.exe"
        if (-not (Test-Path $windeploy)) { throw "missing windeployqt: $windeploy" }
        & $windeploy --release --no-translations --no-system-d3d-compiler --no-opengl-sw $exe
        if ($LASTEXITCODE -ne 0) { throw "windeployqt failed with exit code $LASTEXITCODE" }
        # verify-windows.ps1 runs the app with QT_QPA_PLATFORM=offscreen so no
        # window flashes during acceptance; make sure that plugin is present.
        $platforms = Join-Path $appDir "platforms"
        $offscreen = Join-Path $QtDir "plugins\platforms\qoffscreen.dll"
        if ((Test-Path $offscreen) -and (Test-Path $platforms) -and -not (Test-Path (Join-Path $platforms "qoffscreen.dll"))) {
            Copy-Item $offscreen $platforms -Force
        }
        Write-Host "Deployed to $appDir"
    }
    $cli = Join-Path $toolsDir "dhw_render_cli.exe"
    if (Test-Path $cli) { Copy-Item $cli $appDir -Force }
    Write-Host "Copy the whole $appDir folder to the classroom machine; the data folder is created next to the exe."

    if ($Zip) {
        $distDir = Join-Path $root "dist"
        New-Item -ItemType Directory -Force -Path $distDir | Out-Null
        $zipPath = Join-Path $distDir "DaHoWork-win64.zip"
        $stage = Join-Path $env:TEMP ("DaHoWork-package-" + [guid]::NewGuid().ToString("N"))
        New-Item -ItemType Directory -Force -Path $stage | Out-Null
        try {
            # Runtime data, settings and crash logs belong to the machine, not the package.
            Get-ChildItem -Path $appDir -Force | Where-Object {
                $_.Name -notin @("data", "local_settings.json", "style.json", "crash.log", "startup_failure.log")
            } | Copy-Item -Destination $stage -Recurse -Force
            Compress-Archive -Path (Join-Path $stage "*") -DestinationPath $zipPath -Force
        }
        finally {
            if (Test-Path $stage) { Remove-Item $stage -Recurse -Force }
        }
        $zipSize = (Get-Item $zipPath).Length
        Write-Host ("archive: {0} ({1:N1} MB)" -f $zipPath, ($zipSize / 1MB))
    }
}

$item = Get-Item $exe
Write-Host ""
Write-Host "exe: $($item.FullName)"
if (-not $Deploy -and -not $StaticQt) {
    Write-Warning "This exe is not deployable on its own: Qt6*.dll are not next to it yet."
    Write-Warning "Either rerun with -Deploy (runs windeployqt), or put the Qt bin folder on PATH:"
    Write-Warning "  `$env:PATH = \"$QtDir\bin;`$env:PATH\""
}
Write-Host ("size: {0:N1} MB ({1:N0} bytes)" -f ($item.Length / 1MB), $item.Length)
Write-Host "Done."
if ($testFailed) {
    Write-Warning "Unit tests failed; the build was deployed because -Force was given."
    exit 1
}
