param([Parameter(Mandatory=$true)][string]$QtRoot,[string]$Configuration='Release')
$ErrorActionPreference='Stop'
$projectRoot=Split-Path $PSScriptRoot -Parent
$resolvedQt=(Resolve-Path -LiteralPath $QtRoot).Path
Push-Location $projectRoot
try {
  & cmake -S . -B build -G 'Visual Studio 17 2022' -A x64 "-DCMAKE_PREFIX_PATH=$resolvedQt" -DBUILD_TESTING=ON
  if($LASTEXITCODE -ne 0){throw 'CMake configuration failed'}
  & cmake --build build --config $Configuration --parallel 4
  if($LASTEXITCODE -ne 0){throw 'C++ build failed'}
} finally {Pop-Location}
