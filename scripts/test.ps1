param([Parameter(Mandatory=$true)][string]$QtRoot)
$ErrorActionPreference='Stop'
$projectRoot=Split-Path $PSScriptRoot -Parent
$env:PATH="$QtRoot/bin;$env:PATH"
$env:QT_QPA_PLATFORM='offscreen'
Push-Location $projectRoot
try {
  & python scripts/import_public_data.py
  if($LASTEXITCODE -ne 0){throw 'Public source data verification failed'}
  & ctest --test-dir build -C Release --output-on-failure --output-junit "$projectRoot/build/module-tests.xml"
  if($LASTEXITCODE -ne 0){throw 'Module tests failed'}
  & python tests/process_test.py --bin build/bin --evidence build/process-evidence
  if($LASTEXITCODE -ne 0){throw 'Real process integration tests failed'}
}finally{Pop-Location}
