param([string]$SdkRoot='D:\Work_software\NetFxSDK\4.8')
$ErrorActionPreference='Stop'
# Use the official SDK headers/libraries without launching an elevated MSI.
# UnrealBuildTool supports HKCU SDK registrations (MicrosoftPlatformSDK.cs).
$repo=Split-Path -Parent $PSScriptRoot
$stage=Join-Path $repo 'artifacts/netfx-sdk-download'
New-Item -ItemType Directory -Path $stage -Force|Out-Null
$catalogFile=Get-ChildItem 'C:\ProgramData\Microsoft\VisualStudio\Packages\_Instances' -Recurse -Filter catalog.json | Select-Object -First 1
if(-not $catalogFile){throw 'A local Microsoft Visual Studio installer catalog is required.'}
$catalog=Get-Content -LiteralPath $catalogFile.FullName -Raw|ConvertFrom-Json
$package=$catalog.packages|Where-Object id -eq 'Microsoft.Net.4.8.SDK'|Select-Object -First 1
if(-not $package){throw 'Microsoft .NET Framework 4.8 SDK was not found in the catalog.'}
foreach($payload in $package.payloads){
    if(([uri]$payload.url).Host -ne 'download.visualstudio.microsoft.com' -or $payload.fileName -notmatch '^sdk_tools48\.(msi|cab)$'){throw 'Unexpected SDK payload.'}
    $out=Join-Path $stage $payload.fileName
    if(-not(Test-Path -LiteralPath $out)){Invoke-WebRequest -Uri $payload.url -OutFile $out}
    if((Get-FileHash -LiteralPath $out -Algorithm SHA256).Hash -ne $payload.sha256){throw 'SDK hash mismatch.'}
}
$extract=Join-Path $stage 'expanded'
New-Item -ItemType Directory -Path $extract -Force|Out-Null
& "$env:WINDIR\System32\expand.exe" '-F:*' (Join-Path $stage 'sdk_tools48.cab') $extract | Out-Null
if($LASTEXITCODE -ne 0){throw 'SDK CAB extraction failed.'}
$installer=New-Object -ComObject WindowsInstaller.Installer
$database=$installer.OpenDatabase((Join-Path $stage 'sdk_tools48.msi'),0)
$view=$database.OpenView('SELECT `File`, `FileName` FROM `File`')
$view.Execute()
$count=0
while($record=$view.Fetch()){
    $id=$record.StringData(1)
    $leaf=($record.StringData(2) -split '\|')[-1]
    $subdir=$null
    if($id -like 'SDK_netfx_headers_*'){$subdir='Include/um'}
    elseif($id -like 'SDK_netfx_libs_amd64_*'){$subdir='Lib/um/x64'}
    elseif($id -like 'SDK_netfx_libs_x86_*'){$subdir='Lib/um/x86'}
    elseif($id -like 'SDK_netfx_libs_arm_*'){$subdir='Lib/um/arm'}
    if(-not $subdir){continue}
    if([IO.Path]::GetFileName($leaf) -ne $leaf){throw 'Unexpected SDK file name.'}
    $destination=Join-Path $SdkRoot $subdir
    New-Item -ItemType Directory -Path $destination -Force|Out-Null
    Copy-Item -LiteralPath (Join-Path $extract $id) -Destination (Join-Path $destination $leaf) -Force
    ++$count
}
if(-not(Test-Path (Join-Path $SdkRoot 'Include/um/mscoree.h')) -or -not(Test-Path (Join-Path $SdkRoot 'Lib/um/x64/mscoree.lib'))){throw 'Required SDK headers/libraries are missing.'}
$registration='HKCU:\SOFTWARE\Microsoft\Microsoft SDKs\NETFXSDK\4.8'
New-Item -Path $registration -Force|Out-Null
New-ItemProperty -LiteralPath $registration -Name KitsInstallationFolder -PropertyType String -Value ($SdkRoot.TrimEnd('\')+'\') -Force|Out-Null
[ordered]@{source=$catalogFile.FullName;package=$package.id;files=$count;sdk=$SdkRoot;registration=$registration;payloads=@($package.payloads|Select-Object fileName,sha256,url)}|ConvertTo-Json -Depth 5|Set-Content -LiteralPath (Join-Path $stage 'verification.json') -Encoding utf8
Write-Output "Verified and registered $count official .NET Framework SDK header/library files for the current user: $SdkRoot"
