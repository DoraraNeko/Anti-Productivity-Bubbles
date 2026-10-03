param([string]$Version = "1.0.0")
$ErrorActionPreference = "Stop"
Set-Location (Split-Path $PSScriptRoot -Parent)
$msbuild = "C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe"
if (-not (Test-Path $msbuild)) { $msbuild = "MSBuild" }
& $msbuild APB.vcxproj /t:Rebuild /p:Configuration=Release /p:Platform=x64 /m /v:m
if ($LASTEXITCODE -ne 0) { throw "Build failed" }

$name = "APB-v$Version"
$stage = Join-Path "dist" $name
if (Test-Path "dist") { Remove-Item "dist" -Recurse -Force }
New-Item -ItemType Directory -Path $stage | Out-Null
Copy-Item "bin\x64\Release\APB.exe" $stage
Copy-Item "settings.ini","LICENSE","CHANGELOG.md","README.md" $stage
Copy-Item "README" (Join-Path $stage "README") -Recurse

$zip = Join-Path "dist" "$name.zip"
Compress-Archive -Path $stage -DestinationPath $zip
$hash = (Get-FileHash $zip -Algorithm SHA256).Hash.ToLower()
"$hash  $name.zip" | Set-Content (Join-Path "dist" "SHA256SUMS.txt") -Encoding ASCII
Write-Host "Created $zip"
Write-Host "SHA256: $hash"
