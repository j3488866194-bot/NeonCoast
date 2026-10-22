$ErrorActionPreference='Stop'
$repo=Split-Path -Parent $PSScriptRoot
$path=Join-Path $repo 'Unreal/Config/DefaultCrypto.ini'
# UE embeds this per-build content key in the executable. Never print it or
# distribute the raw config; it protects licensed cooked assets from easy extraction.
$key=$null
if(Test-Path -LiteralPath $path){
    $match=[regex]::Match([IO.File]::ReadAllText($path),'(?m)^EncryptionKey=(.+)\r?$')
    if($match.Success){$key=$match.Groups[1].Value.Trim()}
    if(-not $key -or [Convert]::FromBase64String($key).Length -ne 32){throw 'Existing local content key is invalid; preserve it for investigation.'}
}else{
    $bytes=New-Object byte[] 32
    $rng=[Security.Cryptography.RandomNumberGenerator]::Create()
    try{$rng.GetBytes($bytes)}finally{$rng.Dispose()}
    $key=[Convert]::ToBase64String($bytes)
}
$config=@"
[/Script/CryptoKeys.CryptoKeysSettings]
EncryptionKey=$key
bEncryptAllAssetFiles=True
bEncryptPakIndex=True
bEncryptPakIniFiles=True
bEncryptUAssetFiles=True
bEnablePakSigning=False
"@
& git -C $repo check-ignore -q -- 'Unreal/Config/DefaultCrypto.ini'
if($LASTEXITCODE -ne 0){throw 'Local content-key configuration must be ignored by Git before creation.'}
[IO.File]::WriteAllText($path,$config+"`r`n",[Text.UTF8Encoding]::new($false))
Write-Host 'Local AES-256 content encryption configured; key is excluded from source control.'
