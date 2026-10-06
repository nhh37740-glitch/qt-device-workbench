param(
    [ValidateRange(0,86400)][int]$Seconds=0,
    [ValidateRange(0,65535)][int]$DevicePort=0,
    [ValidateRange(0,65535)][int]$SinkPort=0,
    [string]$OutputDirectory=''
)
$ErrorActionPreference='Stop'
$duration=$Seconds*1000
$serviceDuration=if($duration -gt 0){$duration+10000}else{0}
$runtime=if($OutputDirectory){[IO.Path]::GetFullPath($OutputDirectory)}else{Join-Path $PSScriptRoot 'runtime'}
New-Item -ItemType Directory -Force -Path $runtime | Out-Null
$runId=[Guid]::NewGuid().ToString('N')
$deviceReady=Join-Path $runtime "device-ready-$runId.json"
$sinkReady=Join-Path $runtime "sink-ready-$runId.json"
function Wait-ReadyPort($Path,$Process){
    $timer=[Diagnostics.Stopwatch]::StartNew()
    while($timer.Elapsed.TotalSeconds -lt 10){
        if($Process.HasExited){throw "Service exited before listening: $($Process.Id)"}
        if(Test-Path -LiteralPath $Path){return [int](Get-Content -LiteralPath $Path -Raw | ConvertFrom-Json).port}
        Start-Sleep -Milliseconds 50
    }
    throw 'Service did not report a listening port within 10 seconds'
}
$services=@()
try {
    $services+=Start-Process -FilePath "$PSScriptRoot/programs/device-simulator/device-simulator.exe" -ArgumentList '--port',$DevicePort,'--duration-ms',$serviceDuration,'--ready-file',('"'+$deviceReady+'"') -WindowStyle Hidden -PassThru
    $actualDevicePort=Wait-ReadyPort $deviceReady $services[-1]
    $services+=Start-Process -FilePath "$PSScriptRoot/programs/result-receiver/result-receiver.exe" -ArgumentList '--port',$SinkPort,'--duration-ms',$serviceDuration,'--output',('"'+$runtime+'/downstream.ndjson"'),'--ready-file',('"'+$sinkReady+'"') -WindowStyle Hidden -PassThru
    $actualSinkPort=Wait-ReadyPort $sinkReady $services[-1]
    $dashboard=Start-Process -FilePath "$PSScriptRoot/programs/device-workbench/device-workbench.exe" -ArgumentList '--capture','--device-port',$actualDevicePort,'--sink-port',$actualSinkPort,'--record',('"'+$runtime+'/measurements.csv"'),'--report',('"'+$runtime+'/demo-report.json"'),'--duration-ms',$duration -PassThru
    $dashboard.WaitForExit()
    if($dashboard.ExitCode -ne 0){throw "Dashboard exited with code $($dashboard.ExitCode)"}
} finally {
    foreach($service in $services){if(-not $service.HasExited){Stop-Process -Id $service.Id}}
}
