[CmdletBinding()]
param()

$ErrorActionPreference = "Stop"
$repo = Split-Path -Parent $PSScriptRoot
$android = Join-Path $repo "android"
$downloads = Join-Path $android ".downloads"
$deps = Join-Path $android ".deps"

New-Item -ItemType Directory -Force -Path $downloads, $deps | Out-Null

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

$archives = @(
    @{
        Name = "SDL2-2.32.10.zip"
        Url = "https://github.com/libsdl-org/SDL/releases/download/release-2.32.10/SDL2-2.32.10.zip"
        Sha = "12b2dc2eb8f2836100a7916b5d394a0c82f1f7e32693f95f98305403af242f08"
        Directory = "SDL2-2.32.10"
    },
    @{
        Name = "SDL2_image-2.8.12.zip"
        Url = "https://github.com/libsdl-org/SDL_image/releases/download/release-2.8.12/SDL2_image-2.8.12.zip"
        Sha = "fd6318fd686c2f7049dd19974f957d53c4025ca2b9b6119f8e8ad962d0e3c113"
        Directory = "SDL2_image-2.8.12"
    },
    @{
        Name = "SDL2_mixer-2.8.2.zip"
        Url = "https://github.com/libsdl-org/SDL_mixer/releases/download/release-2.8.2/SDL2_mixer-2.8.2.zip"
        Sha = "4211ce9208fdefb22b3114992fbe2ec280b8d0eabf368a28308b59ecb81cdf19"
        Directory = "SDL2_mixer-2.8.2"
    },
    @{
        Name = "SDL2_ttf-2.24.0.zip"
        Url = "https://github.com/libsdl-org/SDL_ttf/releases/download/release-2.24.0/SDL2_ttf-2.24.0.zip"
        Sha = "bef50614acb63347fe1612facabb31b0f1a05ca2b5c271a619c26fdae561a1c9"
        Directory = "SDL2_ttf-2.24.0"
    }
)

foreach($item in $archives) {
    $archive = Join-Path $downloads $item.Name
    $destination = Join-Path $deps $item.Directory
    $marker = Join-Path $destination "CMakeLists.txt"

    if(Test-Path -LiteralPath $marker) { continue }

    Get-VerifiedArchive -Url $item.Url -Destination $archive -Sha256 $item.Sha
    Expand-Archive -LiteralPath $archive -DestinationPath $deps -Force
    if(-not (Test-Path -LiteralPath $marker)) {
        throw "Archive did not create expected path: $marker"
    }
}

Write-Host "Pinned SDL Android sources ready under $deps"
