[CmdletBinding()]
param(
    [ValidateSet("Debug", "Release")]
    [string]$Configuration = "Debug",
    [string]$VersionName = "0.4.0-dev",
    [switch]$Bootstrap,
    [switch]$Clean
)

$ErrorActionPreference = "Stop"
$repo = Split-Path -Parent $PSScriptRoot
$android = Join-Path $repo "android"

if($Bootstrap) {
    & (Join-Path $PSScriptRoot "bootstrap-android.ps1")
}

$javaHome = Join-Path $android ".jdk\jdk-17.0.16+8"
$sdkRoot = Join-Path $android ".sdk"
$gradle = Join-Path $android ".gradle\gradle-8.10.2\bin\gradle.bat"

foreach($required in @(
    (Join-Path $javaHome "bin\java.exe"),
    (Join-Path $sdkRoot "ndk\27.2.12479018\build\cmake\android.toolchain.cmake"),
    $gradle
)) {
    if(-not (Test-Path -LiteralPath $required)) {
        throw "Android toolchain is incomplete: $required. Run scripts/bootstrap-android.ps1."
    }
}

$env:JAVA_HOME = $javaHome
$env:ANDROID_HOME = $sdkRoot
$env:ANDROID_SDK_ROOT = $sdkRoot
$env:PATH = "$(Join-Path $javaHome 'bin');$(Join-Path $sdkRoot 'platform-tools');$env:PATH"

$variant = $Configuration.ToLowerInvariant()
$gradleArgs = @(
    "--project-dir", $android,
    "--stacktrace",
    "-PfourWindsVersionName=$VersionName"
)
if($Clean) { $gradleArgs += "clean" }
$gradleArgs += ":app:assemble$Configuration"

& $gradle @gradleArgs
if($LASTEXITCODE -ne 0) {
    throw "Gradle Android build failed with exit code $LASTEXITCODE."
}

$apk = Join-Path $android "app\build\outputs\apk\$variant\app-$variant.apk"
if(-not (Test-Path -LiteralPath $apk)) {
    throw "Gradle completed but APK was not found: $apk"
}

$dist = Join-Path $repo "dist\android"
New-Item -ItemType Directory -Force -Path $dist | Out-Null
$suffix = if($Configuration -eq "Debug") { "debug" } else { "release-unsigned" }
$output = Join-Path $dist "four-winds-reborn-v$VersionName-android-arm64-$suffix.apk"
Copy-Item -LiteralPath $apk -Destination $output -Force
& (Join-Path $PSScriptRoot "test-android-package.ps1") `
    -Apk $output `
    -ExpectedVersionName $VersionName `
    -AllowUnsigned:($Configuration -eq "Release")
Write-Host "Android APK: $output"
