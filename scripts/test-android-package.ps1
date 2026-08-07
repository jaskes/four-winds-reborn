[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$Apk,
    [string]$ExpectedVersionName = "0.4.0-dev",
    [switch]$AllowUnsigned
)

$ErrorActionPreference = "Stop"
$repo = Split-Path -Parent $PSScriptRoot
$sdkRoot = Join-Path $repo "android\.sdk"
$aapt = Join-Path $sdkRoot "build-tools\35.0.0\aapt2.exe"
$apksigner = Join-Path $sdkRoot "build-tools\35.0.0\apksigner.bat"
$resolvedApk = (Resolve-Path -LiteralPath $Apk).Path

foreach($required in @($aapt, $apksigner)) {
    if(-not (Test-Path -LiteralPath $required)) {
        throw "Android package tool is missing: $required"
    }
}

$badging = (& $aapt dump badging $resolvedApk 2>&1) -join "`n"
if($LASTEXITCODE -ne 0) {
    throw "aapt2 could not inspect $resolvedApk`n$badging"
}
$escapedVersionName = [regex]::Escape($ExpectedVersionName)
if($badging -notmatch "package: name='com\.jaskes\.fourwindsreborn' versionCode='400' versionName='$escapedVersionName'") {
    throw "Unexpected Android package identity.`n$badging"
}
if($badging -notmatch "native-code: 'arm64-v8a'") {
    throw "APK does not declare the arm64-v8a ABI.`n$badging"
}

Add-Type -AssemblyName System.IO.Compression.FileSystem
$archive = [System.IO.Compression.ZipFile]::OpenRead($resolvedApk)
try {
    $entries = @($archive.Entries | ForEach-Object FullName)
    $requiredEntries = @(
        "lib/arm64-v8a/libSDL2.so",
        "lib/arm64-v8a/libSDL2_image.so",
        "lib/arm64-v8a/libSDL2_mixer.so",
        "lib/arm64-v8a/libSDL2_ttf.so",
        "lib/arm64-v8a/libc++_shared.so",
        "lib/arm64-v8a/libmain.so",
        "assets/assets.list"
    )
    foreach($entry in $requiredEntries) {
        if($entries -notcontains $entry) {
            throw "APK entry is missing: $entry"
        }
    }
    foreach($theme in @("classic", "reborn")) {
        if(-not ($entries | Where-Object { $_ -like "assets/themes/$theme/*" } | Select-Object -First 1)) {
            throw "APK contains no files for the $theme theme."
        }
    }
}
finally {
    $archive.Dispose()
}

if(-not $AllowUnsigned) {
    & $apksigner verify --verbose $resolvedApk | Out-Host
    if($LASTEXITCODE -ne 0) {
        throw "APK signature verification failed with exit code $LASTEXITCODE."
    }
}

$sizeMiB = [math]::Round((Get-Item -LiteralPath $resolvedApk).Length / 1MB, 1)
Write-Host "Android package verified: $resolvedApk ($sizeMiB MiB)"
