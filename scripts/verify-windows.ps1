<#
    Windows acceptance check for an already built tree.

    Usage:
        powershell -ExecutionPolicy Bypass -File scripts\verify-windows.ps1 `
            -BuildDir build -Config Release
        powershell -ExecutionPolicy Bypass -File scripts\verify-windows.ps1 -Static

    Checks:
      1. ctest is green (157 passed, 0 failed in the unit test binary).
      2. DaHoWork.exe --selfcheck prints "selfcheck failures=0" and exits 0.
      3. DaHoWork.exe --list-styles picks windows11 on Windows 11 and
         modernwindows on Windows 10.
      4. DaHoWork.exe --render writes a 1920x1080 PNG.
      5. A plain run creates data\ next to the executable.
      6. With -Static: the import table of DaHoWork.exe (and dhw_render_cli.exe)
         contains no Qt6*.dll and no MSVC runtime DLLs (dumpbin /dependents).

    Any failed step exits 1. QT_QPA_PLATFORM=offscreen is set for every run,
    so this script never opens a visible window.
#>
param(
    [string]$BuildDir = "build",
    [string]$Config = "Release",
    [switch]$Static
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
Set-Location $root
$env:QT_QPA_PLATFORM = "offscreen"

function Stop-Check([string]$message) {
    Write-Host "FAIL: $message" -ForegroundColor Red
    exit 1
}

function Get-PngSize([string]$path) {
    $bytes = [System.IO.File]::ReadAllBytes($path)
    if ($bytes.Length -lt 24) {
        return $null
    }
    $width = ([int]($bytes[16]) -shl 24) -bor ([int]($bytes[17]) -shl 16) -bor `
             ([int]($bytes[18]) -shl 8) -bor [int]($bytes[19])
    $height = ([int]($bytes[20]) -shl 24) -bor ([int]($bytes[21]) -shl 16) -bor `
              ([int]($bytes[22]) -shl 8) -bor [int]($bytes[23])
    return @{ Width = $width; Height = $height }
}

# The -Static checks read the PE import table. dumpbin ships with the MSVC toolset but
# is not on PATH outside a developer shell, so fall back to what vswhere reports.
# (Mirrored in build-windows.ps1 so both scripts stay standalone.)
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

function Assert-StaticExecutable([string]$exePath) {
    $deps = Get-PeDependencies $exePath
    if ($null -eq $deps) {
        Stop-Check ("could not read the import table of $exePath (dumpbin.exe not found or its output " +
                    "could not be parsed); run this check from a Visual Studio developer shell")
    }
    Write-Host ("imports: " + ($deps -join ", "))
    # Any Qt DLL or dynamic MSVC/UCRT DLL means the exe is not self-contained.
    $forbidden = @($deps | Where-Object {
        $_ -match '^(Qt6|VCRUNTIME140|MSVCP140|CONCRT140|ucrtbase|api-ms-win-crt-)'
    })
    if ($forbidden.Count -gt 0) {
        Stop-Check ("$exePath still imports " + ($forbidden -join ", ") +
                    "; it is not self-contained (rebuild with -StaticQt against a static Qt kit)")
    }
}

# The build tree may come from a multi-config generator (Visual Studio) or a
# single-config one (Ninja / Makefiles); read it back instead of guessing.
$generator = ""
$cacheFile = Join-Path $BuildDir "CMakeCache.txt"
if (Test-Path $cacheFile) {
    $match = Select-String -Path $cacheFile -Pattern '^CMAKE_GENERATOR:INTERNAL=(.+)$' | Select-Object -First 1
    if ($match) { $generator = $match.Matches[0].Groups[1].Value.Trim() }
}
$multiConfig = $generator -like "Visual Studio*" -or $generator -like "Xcode*"
if ($multiConfig) {
    $appDir = Join-Path $BuildDir "apps\qt\$Config"
    $toolsDir = Join-Path $BuildDir "tools\$Config"
} else {
    $appDir = Join-Path $BuildDir "apps\qt"
    $toolsDir = Join-Path $BuildDir "tools"
}
Write-Host ("build dir generator: {0}" -f $(if ($generator) { $generator } else { "unknown" }))
# -Static adds one acceptance step; keep the "n/total" numbering honest.
$totalSteps = if ($Static) { 6 } else { 5 }
$exe = Join-Path $appDir "DaHoWork.exe"
if (-not (Test-Path $exe)) {
    Stop-Check "missing $exe; run scripts\build-windows.ps1 first"
}

$dataDir = Join-Path $appDir "data"
$dataExisted = Test-Path $dataDir
$tempData = Join-Path $env:TEMP ("DaHoWork-verify-" + [guid]::NewGuid().ToString("N"))
$tempOk = $false

try {
    New-Item -ItemType Directory -Force -Path $tempData | Out-Null
    Copy-Item (Join-Path $root "fixtures\demo\data.json") (Join-Path $tempData "data.json") -Force
    $tempOk = $true

    Write-Host "== 1/$totalSteps ctest =="
    Write-Host "expected: every test passes (the current tree reports 157 passed, 0 failed)"
    ctest --test-dir $BuildDir -C $Config --output-on-failure
    if ($LASTEXITCODE -ne 0) { Stop-Check "ctest exited with code $LASTEXITCODE" }
    Write-Host "ok: ctest"

    # DaHoWork.exe is a GUI-subsystem binary: its stdio handles are only reliable when
    # the caller really redirects them. cmd.exe file redirection always creates a real
    # handle, so diagnostics are captured through that instead of a PowerShell pipe.
    function Invoke-Captured([string]$program, [string[]]$programArgs) {
        $log = Join-Path $env:TEMP ("dahowork-" + [guid]::NewGuid().ToString("N") + ".log")
        $quoted = ($programArgs | ForEach-Object { '"' + $_ + '"' }) -join " "
        $line = '"' + $program + '" ' + $quoted + ' > "' + $log + '" 2>&1'
        cmd.exe /c $line | Out-Null
        $exit = $LASTEXITCODE
        $text = ""
        if (Test-Path $log) {
            $text = Get-Content -Path $log -Raw
            Remove-Item $log -Force -ErrorAction SilentlyContinue
        }
        return [pscustomobject]@{ Code = $exit; Text = $text }
    }

    Write-Host ""
    Write-Host "== 2/$totalSteps selfcheck =="
    Write-Host "expected: selfcheck failures=0"
    $result = Invoke-Captured $exe @("--data-dir", $tempData, "--selfcheck")
    $selfcheck = $result.Text
    Write-Host $selfcheck.Trim()
    if ($result.Code -ne 0) { Stop-Check "selfcheck exited with code $($result.Code)" }
    if ($selfcheck -notmatch "selfcheck failures=0") {
        Stop-Check "selfcheck did not report failures=0"
    }
    Write-Host "ok: selfcheck"

    Write-Host ""
    Write-Host "== 3/$totalSteps widget style =="
    $buildNumber = 0
    try {
        $buildNumber = [int]((Get-ItemProperty -Path "HKLM:\SOFTWARE\Microsoft\Windows NT\CurrentVersion" -Name CurrentBuildNumber).CurrentBuildNumber)
    } catch {
        $buildNumber = [Environment]::OSVersion.Version.Build
    }
    if ($buildNumber -ge 22000) {
        $expectedStyle = "windows11"
        $osName = "Windows 11"
    } else {
        $expectedStyle = "modernwindows"
        $osName = "Windows 10"
    }
    Write-Host "OS: $osName (build $buildNumber); expected selected style: $expectedStyle"
    $result = Invoke-Captured $exe @("--list-styles")
    $styles = $result.Text
    Write-Host $styles.Trim()
    if ($result.Code -ne 0) { Stop-Check "--list-styles exited with code $($result.Code)" }
    if ($styles -notmatch "selected style:\s*(\S+)") {
        Stop-Check "could not find the selected style in --list-styles output"
    }
    $selectedStyle = $Matches[1]
    if ($selectedStyle -ne $expectedStyle) {
        Stop-Check "selected style is $selectedStyle, expected $expectedStyle on $osName"
    }
    Write-Host "ok: style $selectedStyle"

    Write-Host ""
    Write-Host "== 4/$totalSteps render =="
    Write-Host "expected: 1920x1080 PNG"
    $renderDir = Join-Path $BuildDir "verify"
    New-Item -ItemType Directory -Force -Path $renderDir | Out-Null
    $renderPath = Join-Path $renderDir "render-1920x1080.png"
    if (Test-Path $renderPath) { Remove-Item $renderPath -Force }
    # No --data-dir here: this is the run that must create data\ next to the exe.
    $render = & $exe --render $renderPath 2>&1 | Out-String
    $code = $LASTEXITCODE
    Write-Host $render.Trim()
    if ($code -ne 0) { Stop-Check "--render exited with code $code" }
    if (-not (Test-Path $renderPath)) { Stop-Check "render did not write $renderPath" }
    $size = Get-PngSize $renderPath
    if ($null -eq $size) { Stop-Check "render output is not a PNG" }
    Write-Host "render size: $($size.Width)x$($size.Height)"
    if ($size.Width -ne 1920 -or $size.Height -ne 1080) {
        Stop-Check "render is $($size.Width)x$($size.Height), expected 1920x1080"
    }
    $pngBytes = (Get-Item $renderPath).Length
    Write-Host ("ok: {0} ({1:N0} bytes)" -f $renderPath, $pngBytes)

    Write-Host ""
    Write-Host "== 5/$totalSteps data directory next to the exe =="
    Write-Host "expected: data\ is created next to DaHoWork.exe on first run"
    if (-not (Test-Path $dataDir)) {
        Stop-Check "missing $dataDir after a plain run"
    }
    Write-Host "ok: $dataDir exists"
}
finally {
    if ($tempOk -and (Test-Path $tempData)) {
        Remove-Item $tempData -Recurse -Force
    }
    if (-not $dataExisted -and (Test-Path $dataDir)) {
        # Keep the deployed folder clean for -Zip; only remove what this check made.
        Remove-Item $dataDir -Recurse -Force
        Write-Host "cleaned the data\ folder created by this check"
    }
}

if ($Static) {
    Write-Host ""
    Write-Host "== 6/$totalSteps static self-containment =="
    Write-Host "expected: only Windows system DLLs; no Qt6*.dll, VCRUNTIME140*.dll, MSVCP140*.dll"
    Assert-StaticExecutable $exe
    $cli = Join-Path $toolsDir "dhw_render_cli.exe"
    if (Test-Path $cli) {
        Assert-StaticExecutable $cli
    }
    Write-Host "ok: no Qt or MSVC runtime DLL imports"
}

Write-Host ""
Write-Host "All Windows checks passed."
Write-Host "artifacts:"
Write-Host "  $exe"
Write-Host "  $(Join-Path $BuildDir 'verify\render-1920x1080.png')"
