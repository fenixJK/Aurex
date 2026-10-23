param([string]$BuildDir = 'build', [Parameter(Mandatory=$true)][string]$NoticesFile)
$ErrorActionPreference = 'Stop'
$version = (Get-Content "$BuildDir/release-version.txt" -Raw).Trim()
$destination = "dist/Aurex-$version-Windows-x64"
if (Test-Path $destination) { Remove-Item $destination -Recurse -Force }
New-Item -ItemType Directory -Force $destination | Out-Null
Copy-Item "$BuildDir/Aurex.exe" $destination
Copy-Item LICENSE -Destination $destination
Copy-Item $NoticesFile -Destination "$destination/THIRD_PARTY_NOTICES.txt"
$imports = & dumpbin /dependents "$destination/Aurex.exe"
if ($LASTEXITCODE) { throw 'Could not inspect executable dependencies' }
$dependencies = @($imports | Select-String '^\s+([^\s]+\.dll)\s*$' | ForEach-Object { $_.Matches.Groups[1].Value })
if (!$dependencies.Count) { throw 'Executable dependency list is empty' }
foreach ($dependency in $dependencies) {
    if ($dependency -match '^(Qt[0-9]|libcrypto|libssl|opencv|vcruntime|msvcp|msvcr|ucrtbase|api-ms-win-crt-)' -or
        ($dependency -notmatch '^(api-ms-|ext-ms-)' -and !(Test-Path "$env:SystemRoot/System32/$dependency"))) {
        throw "Executable still requires an external library: $dependency"
    }
}
Get-ChildItem Env: | Where-Object { $_.Name -like 'QT_*' -or $_.Name -like 'QML*' } | ForEach-Object { Remove-Item "Env:$($_.Name)" }
$env:QT_QPA_PLATFORM = 'windows'
$env:PATH = "$env:SystemRoot/system32;$env:SystemRoot"
$smoke = "$env:RUNNER_TEMP/Aurex package smoke"
if (Test-Path $smoke) { Remove-Item $smoke -Recurse -Force }
New-Item -ItemType Directory -Force $smoke | Out-Null
Copy-Item "$destination/Aurex.exe" "$smoke/Aurex.exe"
$screenshot = "$smoke/startup.png"
$process = Start-Process "$smoke/Aurex.exe" -ArgumentList "--screenshot `"$screenshot`"" -WorkingDirectory $env:RUNNER_TEMP -PassThru
if (!$process.WaitForExit(30000)) { Stop-Process $process.Id; throw 'Packaged application did not start' }
if ($process.ExitCode -ne 0 -or !(Test-Path $screenshot)) { throw 'Packaged application startup failed' }
Remove-Item $smoke -Recurse -Force
Compress-Archive -Path $destination -DestinationPath "$destination.zip" -CompressionLevel Optimal -Force
