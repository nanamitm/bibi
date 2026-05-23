$ErrorActionPreference = "Stop"

$root = Split-Path -Parent $MyInvocation.MyCommand.Path
$vcvars = "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat"
$cmake = "C:\Qt\Tools\CMake_64\bin\cmake.exe"

if (-not (Test-Path -LiteralPath $vcvars)) {
    $vcvars = "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
}

if (-not (Test-Path -LiteralPath $vcvars)) {
    throw "Visual Studio vcvars64.bat was not found."
}

if (-not (Test-Path -LiteralPath $cmake)) {
    throw "Qt CMake was not found: $cmake"
}

Push-Location $root
try {
    & cmd /c "`"$vcvars`" && `"$cmake`" --build build --parallel"
    if ($LASTEXITCODE -ne 0) {
        exit $LASTEXITCODE
    }
} finally {
    Pop-Location
}
