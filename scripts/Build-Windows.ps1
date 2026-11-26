[CmdletBinding()]
param([string]$JucePath = "", [ValidateRange(1,64)][int]$Jobs = 2)
$ErrorActionPreference = "Stop"
$ProjectRoot = Split-Path -Parent $PSScriptRoot
$BuildDir = Join-Path $ProjectRoot "build-windows"
$DistDir = Join-Path $ProjectRoot "dist"
$BundleName = "Continuum Rack.vst3"
function Run-Checked {
    param([string]$Program, [string[]]$Arguments)
    & $Program @Arguments
    if ($LASTEXITCODE -ne 0) { throw "$Program failed with exit code $LASTEXITCODE." }
}
try {
    if (-not [Environment]::Is64BitOperatingSystem) { throw "This project targets Windows x64." }
    $ProgramFilesX86 = [Environment]::GetFolderPath([Environment+SpecialFolder]::ProgramFilesX86)
    $VsWhere = Join-Path $ProgramFilesX86 "Microsoft Visual Studio\Installer\vswhere.exe"
    if (-not (Test-Path -LiteralPath $VsWhere)) {
        throw "Install Visual Studio 2022 Build Tools with Desktop development with C++, including a Windows SDK and CMake tools."
    }
    $VsPath = & $VsWhere -latest -products * -version "[17.0,18.0)" -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if ($LASTEXITCODE -ne 0 -or -not $VsPath) { throw "Visual Studio 2022 C++ tools were not found. See START_HERE.md." }
    $CMakeCommand = Get-Command cmake.exe -ErrorAction SilentlyContinue
    if ($CMakeCommand) { $CMake = $CMakeCommand.Source }
    else { $CMake = Join-Path $VsPath "Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" }
    if (-not (Test-Path -LiteralPath $CMake)) { throw "Install CMake 3.24+ or the Visual Studio C++ CMake tools component." }
    $VersionLine = (& $CMake --version | Select-Object -First 1)
    if ($LASTEXITCODE -ne 0 -or $VersionLine -notmatch "cmake version (\d+\.\d+\.\d+)") { throw "Could not determine the CMake version." }
    if ([version]$Matches[1] -lt [version]"3.24.0") { throw "CMake 3.24+ is required; found $VersionLine." }
    $CTest = Join-Path (Split-Path -Parent $CMake) "ctest.exe"
    if (-not (Test-Path -LiteralPath $CTest)) { throw "ctest.exe must be beside cmake.exe." }
    $Configure = @("-S", $ProjectRoot, "-B", $BuildDir, "-G", "Visual Studio 17 2022", "-A", "x64",
                   "-DCMAKE_GENERATOR_INSTANCE=$VsPath", "-DBUILD_TESTING=ON",
                   "-DRACK_BUILD_PLUGIN=ON", "-DRACK_JUCE_PATH=$JucePath")
    Write-Host "Configuring Continuum Rack. The first build downloads pinned JUCE 8.0.12."
    Run-Checked -Program $CMake -Arguments $Configure
    Run-Checked -Program $CMake -Arguments @("--build", $BuildDir, "--config", "Release", "--parallel", "$Jobs")
    Run-Checked -Program $CTest -Arguments @("--test-dir", $BuildDir, "-C", "Release", "--output-on-failure")
    $BuiltBundle = Join-Path $BuildDir "ContinuumRack_artefacts\Release\VST3\$BundleName"
    if (-not (Test-Path -LiteralPath $BuiltBundle -PathType Container)) { throw "Expected VST3 bundle missing: $BuiltBundle" }
    New-Item -ItemType Directory -Path $DistDir -Force | Out-Null
    $Destination = Join-Path $DistDir $BundleName
    # Replace only this project's generated dist bundle.
    if (Test-Path -LiteralPath $Destination) { Remove-Item -LiteralPath $Destination -Recurse -Force }
    Copy-Item -LiteralPath $BuiltBundle -Destination $DistDir -Recurse
    $BuiltApp = Join-Path $BuildDir "ContinuumRack_artefacts\Release\Standalone\Continuum Rack.exe"
    if (Test-Path -LiteralPath $BuiltApp) { Copy-Item -LiteralPath $BuiltApp -Destination $DistDir -Force }
    Write-Host ""
    Write-Host "Build and automated tests passed."
    Write-Host "VST3 bundle: $Destination"
    Write-Host "Standalone app: $(Join-Path $DistDir 'Continuum Rack.exe')"
    Write-Host "Copy the whole .vst3 folder using START_HERE.md, then run docs\DAW_CHECKLIST.md."
    exit 0
} catch { Write-Error $_ -ErrorAction Continue; exit 1 }

