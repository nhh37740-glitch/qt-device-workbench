param([Parameter(Mandatory=$true)][string]$QtRoot,[string]$Version='1.1.0')
$ErrorActionPreference='Stop'
$projectRoot=Split-Path $PSScriptRoot -Parent
$resolvedQt=(Resolve-Path -LiteralPath $QtRoot).Path
$packageRoot=Join-Path $projectRoot "build/package-$([DateTimeOffset]::UtcNow.ToUnixTimeMilliseconds())"
$env:PATH="$resolvedQt/bin;$env:PATH"
New-Item -ItemType Directory -Force -Path $packageRoot | Out-Null
Push-Location $projectRoot
try {
    & cmake --install build --config Release --prefix "$packageRoot/shared"
    if($LASTEXITCODE -ne 0){throw 'Binary installation failed'}
    $runtime=Join-Path $packageRoot 'shared/bin'
    foreach($exe in 'device-workbench','device-simulator','result-receiver'){
        & "$resolvedQt/bin/windeployqt.exe" --release --no-translations --no-opengl-sw --no-system-d3d-compiler --compiler-runtime --dir $runtime "$runtime/$exe.exe"
        if($LASTEXITCODE -ne 0){throw "Qt runtime deployment failed: $exe"}
    }
    # Explicit runtime fallback for CLI/module import dependencies, independent of Qt deploy scanning.
    foreach($name in 'Core','Gui','Widgets','Network'){
        Copy-Item -LiteralPath "$resolvedQt/bin/Qt6$name.dll" -Destination $runtime -Force
    }
    # The deployment scanner selects the visible Windows plugin; unattended
    # screenshot and DLL-consumer checks also need the headless platform plugin.
    Copy-Item -LiteralPath "$resolvedQt/plugins/platforms/qoffscreen.dll" -Destination "$runtime/platforms" -Force
    $vswhere="${env:ProgramFiles(x86)}/Microsoft Visual Studio/Installer/vswhere.exe"
    $vsRoot=(& $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath).Trim()
    $crt=Get-ChildItem -LiteralPath "$vsRoot/VC/Redist/MSVC" -Recurse -Directory | Where-Object { $_.FullName -match '\\x64\\Microsoft\.VC143\.CRT$' -and $_.FullName -notmatch 'onecore' } | Sort-Object FullName -Descending | Select-Object -First 1
    if(-not $crt){throw 'Cannot locate MSVC x64 runtime for portable delivery'}
    Get-ChildItem -LiteralPath $crt.FullName -Filter '*.dll' | ForEach-Object {Copy-Item -LiteralPath $_.FullName -Destination $runtime -Force}
    $programModules=@{
      'device-workbench'=@('contracts','wire_protocol','device_source','frontend','record_store','result_push')
      'device-simulator'=@('contracts','wire_protocol','device_simulator')
      'result-receiver'=@('contracts','wire_protocol','result_receiver')
    }
    foreach($program in $programModules.Keys){
        $dest=Join-Path $packageRoot "programs/$program"
        New-Item -ItemType Directory -Force -Path $dest | Out-Null
        Get-ChildItem -LiteralPath $runtime | Where-Object { $_.Name -notlike 'wb_*' -and $_.Extension -ne '.exe' } | ForEach-Object {Copy-Item -LiteralPath $_.FullName -Destination $dest -Recurse -Force}
        Copy-Item -LiteralPath "$runtime/$program.exe" -Destination $dest
        foreach($module in $programModules[$program]){Copy-Item -LiteralPath "$runtime/wb_$module.dll" -Destination $dest}
    }
    $moduleDeps=@{
      contracts=@();wire_protocol=@('contracts');device_simulator=@('contracts','wire_protocol');device_source=@('contracts','wire_protocol');
      frontend=@('contracts');record_store=@('contracts');result_push=@('contracts','wire_protocol');result_receiver=@('contracts','wire_protocol')
    }
    $factories=@{wire_protocol='wb_create_wire';device_simulator='wb_create_simulator';device_source='wb_create_source';frontend='wb_create_frontend';record_store='wb_create_store';result_push='wb_create_push';result_receiver='wb_create_receiver'}
    foreach($module in $moduleDeps.Keys){
        $dest=Join-Path $packageRoot "modules/$module"
        New-Item -ItemType Directory -Force -Path "$dest/bin","$dest/lib","$dest/include/workbench" | Out-Null
        Copy-Item -LiteralPath "$runtime/wb_$module.dll" -Destination "$dest/bin"
        Copy-Item -LiteralPath "build/lib/wb_$module.lib" -Destination "$dest/lib"
        Copy-Item -LiteralPath 'include/workbench/contracts.h' -Destination "$dest/include/workbench"
        @{module=$module;version=$Version;abi='Qt6-MSVC2022-x64-C++17';factory=$factories[$module];dependencies=$moduleDeps[$module];runtime='../../shared/bin';header='include/workbench/contracts.h';library="lib/wb_$module.lib";binary="bin/wb_$module.dll"} | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath "$dest/module.json" -Encoding utf8
    }
    Copy-Item -LiteralPath README.md,LICENSE -Destination $packageRoot
    Copy-Item -LiteralPath docs,examples -Destination $packageRoot -Recurse
    New-Item -ItemType Directory -Force -Path "$packageRoot/licenses" | Out-Null
    Copy-Item -LiteralPath LICENSE -Destination "$packageRoot/licenses/project-MIT.txt"
    foreach($license in @('LGPL-3.0-only.txt','GPL-3.0-only.txt')){
       if(Test-Path -LiteralPath "$resolvedQt/doc/global/$license"){Copy-Item -LiteralPath "$resolvedQt/doc/global/$license" -Destination "$packageRoot/licenses"}
    }
    if(Test-Path -LiteralPath "$resolvedQt/sbom"){Copy-Item -LiteralPath "$resolvedQt/sbom" -Destination "$packageRoot/licenses/qt-sbom" -Recurse}
    foreach($license in 'LGPL-3.0-only.txt','GPL-3.0-only.txt','Qt-GPL-exception-1.0.txt'){
       Invoke-WebRequest -Uri "https://raw.githubusercontent.com/qt/qtbase/v6.8.3/LICENSES/$license" -OutFile "$packageRoot/licenses/$license"
    }
    @'
Qt 6.8.3 libraries are dynamically linked and remain replaceable. Qt source and notices:
https://download.qt.io/archive/qt/6.8/6.8.3/single/
https://code.qt.io/cgit/qt/qtbase.git/tree/?h=v6.8.3
Bundled Qt component/third-party notices are in qt-sbom and the accompanying license texts.
Microsoft compiler runtime redistributable DLLs are included for Windows x64 execution.
'@ | Set-Content -LiteralPath "$packageRoot/licenses/README.txt" -Encoding utf8
    # Build only the external example against the delivered SDK, not module sources.
    & cmake -S examples -B build/binary-consumer -G 'Visual Studio 17 2022' -A x64 "-DCMAKE_PREFIX_PATH=$resolvedQt" "-DWORKBENCH_SDK=$packageRoot/shared/sdk"
    if($LASTEXITCODE -ne 0){throw 'Delivered SDK consumer configuration failed'}
    & cmake --build build/binary-consumer --config Release --parallel 2
    if($LASTEXITCODE -ne 0){throw 'Delivered SDK consumer build failed'}
    Copy-Item -LiteralPath build/binary-consumer/Release/binary-consumer.exe -Destination $runtime -Force
    $originalPath=$env:PATH
    $env:PATH="$runtime;$env:SystemRoot/System32"
    try {
       & "$runtime/binary-consumer.exe" $runtime
       if($LASTEXITCODE -ne 0){throw 'Delivered module DLL interface consumer failed'}
    } finally {$env:PATH=$originalPath}
    @'
param([ValidateRange(0,86400)][int]$Seconds=0)
$ErrorActionPreference='Stop'
$duration=$Seconds*1000
$runtime=Join-Path $PSScriptRoot 'runtime'
New-Item -ItemType Directory -Force -Path $runtime | Out-Null
$services=@()
try {
    $services+=Start-Process -FilePath "$PSScriptRoot/programs/device-simulator/device-simulator.exe" -ArgumentList '--duration-ms',$duration -WindowStyle Hidden -PassThru
    $services+=Start-Process -FilePath "$PSScriptRoot/programs/result-receiver/result-receiver.exe" -ArgumentList '--duration-ms',$duration,'--output',('"'+$runtime+'/downstream.ndjson"') -WindowStyle Hidden -PassThru
    $dashboard=Start-Process -FilePath "$PSScriptRoot/programs/device-workbench/device-workbench.exe" -ArgumentList '--capture','--record',('"'+$runtime+'/measurements.csv"'),'--report',('"'+$runtime+'/demo-report.json"'),'--duration-ms',$duration -PassThru
    $dashboard.WaitForExit()
} finally {
    foreach($service in $services){if(-not $service.HasExited){Stop-Process -Id $service.Id}}
}
'@ | Set-Content -LiteralPath "$packageRoot/start-demo.ps1" -Encoding utf8
    @'
@echo off
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0start-demo.ps1"
'@ | Set-Content -LiteralPath "$packageRoot/start-demo.cmd" -Encoding ascii
    & python tests/process_test.py --bin "$packageRoot/programs" --layout programs --evidence build/delivery-evidence
    if($LASTEXITCODE -ne 0){throw 'Binary-only deployment tests failed'}
    New-Item -ItemType Directory -Force -Path "$packageRoot/test-evidence" | Out-Null
    Copy-Item -LiteralPath build/module-tests.xml,build/process-evidence/summary.json -Destination "$packageRoot/test-evidence"
    Copy-Item -LiteralPath build/delivery-evidence/summary.json -Destination "$packageRoot/test-evidence/binary-only-summary.json"
    @{passed=$true;modulesLoaded=7;usesModuleSources=$false;consumer='shared/bin/binary-consumer.exe'} | ConvertTo-Json | Set-Content -LiteralPath "$packageRoot/test-evidence/dll-consumer.json" -Encoding utf8
    $revision=(& git rev-parse HEAD).Trim()
    $files=@(Get-ChildItem -LiteralPath $packageRoot -Recurse -File | ForEach-Object {@{path=[IO.Path]::GetRelativePath($packageRoot,$_.FullName).Replace('\','/');size=$_.Length;sha256=(Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLower()}})
    @{version=$Version;revision=$revision;platform='windows-x64';qt='6.8.3';programs=@('device-workbench','device-simulator','result-receiver');modules=@($moduleDeps.Keys);files=$files} | ConvertTo-Json -Depth 10 | Set-Content -LiteralPath "$packageRoot/manifest.json" -Encoding utf8
    & python scripts/audit_delivery.py --root $packageRoot
    if($LASTEXITCODE -ne 0){throw 'Delivery manifest/binary audit failed'}
    New-Item -ItemType Directory -Force -Path dist | Out-Null
    $zip=Join-Path $projectRoot "dist/qt-device-workbench-$Version-windows-x64.zip"
    Compress-Archive -Path "$packageRoot/*" -DestinationPath $zip -Force
    New-Item -ItemType Directory -Force -Path dist/programs,dist/modules | Out-Null
    foreach($program in $programModules.Keys){Compress-Archive -Path "$packageRoot/programs/$program/*","$packageRoot/licenses" -DestinationPath "dist/programs/$program-$Version-windows-x64.zip" -Force}
    foreach($module in $moduleDeps.Keys){Compress-Archive -Path "$packageRoot/modules/$module/*" -DestinationPath "dist/modules/wb_$module-$Version-windows-x64.zip" -Force}
    @{'packageRoot'=$packageRoot;'zip'=$zip;'sha256'=(Get-FileHash -LiteralPath $zip -Algorithm SHA256).Hash.ToLower()} | ConvertTo-Json | Set-Content -LiteralPath dist/latest.json -Encoding utf8
    Write-Output "Verified binary package: $zip"
} finally {Pop-Location}
