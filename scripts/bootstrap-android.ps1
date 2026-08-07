[CmdletBinding()]
param()

$ErrorActionPreference = "Stop"
$repo = Split-Path -Parent $PSScriptRoot
$android = Join-Path $repo "android"
$downloads = Join-Path $android ".downloads"
$deps = Join-Path $android ".deps"
$jdkRoot = Join-Path $android ".jdk"
$sdkRoot = Join-Path $android ".sdk"
$gradleRoot = Join-Path $android ".gradle"

New-Item -ItemType Directory -Force -Path $downloads, $deps, $jdkRoot, $sdkRoot, $gradleRoot | Out-Null

& (Join-Path $PSScriptRoot "fetch-android-dependencies.ps1")

function Get-VerifiedArchive {
    param(
        [Parameter(Mandatory)][string]$Url,
        [Parameter(Mandatory)][string]$Destination,
        [Parameter(Mandatory)][string]$Sha256
    )
    if(-not (Test-Path -LiteralPath $Destination)) {
        Write-Host "Downloading $Url"
        Invoke-WebRequest -UseBasicParsing -Uri $Url -OutFile $Destination
    }
    $actual = (Get-FileHash -Algorithm SHA256 -LiteralPath $Destination).Hash.ToLowerInvariant()
    if($actual -ne $Sha256.ToLowerInvariant()) {
        Remove-Item -LiteralPath $Destination -Force
        throw "Checksum mismatch for $Destination. Expected $Sha256, got $actual."
    }
}

function Expand-Once {
    param(
        [Parameter(Mandatory)][string]$Archive,
        [Parameter(Mandatory)][string]$Destination,
        [Parameter(Mandatory)][string]$Marker
    )
    if(Test-Path -LiteralPath (Join-Path $Destination $Marker)) { return }
    New-Item -ItemType Directory -Force -Path $Destination | Out-Null
    Expand-Archive -LiteralPath $Archive -DestinationPath $Destination -Force
    if(-not (Test-Path -LiteralPath (Join-Path $Destination $Marker))) {
        throw "Archive did not create expected path: $(Join-Path $Destination $Marker)"
    }
}

$archives = @(
    @{
        Name = "OpenJDK17U-jdk_x64_windows_hotspot_17.0.16_8.zip"
        Url = "https://github.com/adoptium/temurin17-binaries/releases/download/jdk-17.0.16%2B8/OpenJDK17U-jdk_x64_windows_hotspot_17.0.16_8.zip"
        Sha = "8c7cfff78a55c56ebaf470ed6a89c6466b47d8274bdabdda997d7507c20325c5"
        Root = $jdkRoot
        Marker = "jdk-17.0.16+8\bin\java.exe"
    },
    @{
        Name = "gradle-8.10.2-bin.zip"
        Url = "https://services.gradle.org/distributions/gradle-8.10.2-bin.zip"
        Sha = "31c55713e40233a8303827ceb42ca48a47267a0ad4bab9177123121e71524c26"
        Root = $gradleRoot
        Marker = "gradle-8.10.2\bin\gradle.bat"
    }
)

foreach($item in $archives) {
    $archive = Join-Path $downloads $item.Name
    Get-VerifiedArchive -Url $item.Url -Destination $archive -Sha256 $item.Sha
    Expand-Once -Archive $archive -Destination $item.Root -Marker $item.Marker
}

$commandToolsArchive = Join-Path $downloads "commandlinetools-win-15859902_latest.zip"
$commandToolsUrl = "https://dl.google.com/android/repository/commandlinetools-win-15859902_latest.zip"
$commandToolsSha = "90ae805d20434428bffcb699c290860f19bb5f66a67e6b330067e3de801fb04a"
$commandToolsLatest = Join-Path $sdkRoot "cmdline-tools\latest"
if(-not (Test-Path -LiteralPath (Join-Path $commandToolsLatest "bin\sdkmanager.bat"))) {
    Get-VerifiedArchive -Url $commandToolsUrl -Destination $commandToolsArchive -Sha256 $commandToolsSha
    $commandToolsTemp = Join-Path $sdkRoot "cmdline-tools\.extract"
    if(Test-Path -LiteralPath $commandToolsTemp) {
        Remove-Item -LiteralPath $commandToolsTemp -Recurse -Force
    }
    Expand-Archive -LiteralPath $commandToolsArchive -DestinationPath $commandToolsTemp -Force
    $extractedRoot = Join-Path $commandToolsTemp "cmdline-tools"
    if(-not (Test-Path -LiteralPath (Join-Path $extractedRoot "bin\sdkmanager.bat"))) {
        throw "Android command-line tools archive has an unexpected layout."
    }
    New-Item -ItemType Directory -Force -Path (Split-Path -Parent $commandToolsLatest) | Out-Null
    if(Test-Path -LiteralPath $commandToolsLatest) {
        Remove-Item -LiteralPath $commandToolsLatest -Recurse -Force
    }
    Move-Item -LiteralPath $extractedRoot -Destination $commandToolsLatest
    Remove-Item -LiteralPath $commandToolsTemp -Recurse -Force
}

$javaHome = Join-Path $jdkRoot "jdk-17.0.16+8"
$sdkManager = Join-Path $sdkRoot "cmdline-tools\latest\bin\sdkmanager.bat"
$env:JAVA_HOME = $javaHome
$env:ANDROID_HOME = $sdkRoot
$env:ANDROID_SDK_ROOT = $sdkRoot

$licenses = (1..200 | ForEach-Object { "y" }) -join "`n"
$licenses | & $sdkManager --sdk_root=$sdkRoot --licenses | Out-Host
& $sdkManager --sdk_root=$sdkRoot `
    "platform-tools" `
    "platforms;android-35" `
    "build-tools;35.0.0" `
    "cmake;3.22.1" `
    "ndk;27.2.12479018"
if($LASTEXITCODE -ne 0) {
    throw "Android SDK component installation failed with exit code $LASTEXITCODE."
}

Write-Host "Android toolchain ready under $android"
