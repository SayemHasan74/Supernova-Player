[CmdletBinding()]
param(
    [ValidateSet("gpl", "lgpl")]
    [string]$Variant = "gpl",

    [switch]$Latest
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

$PinnedTag = "2026-07-25-8c67647b50"
$PinnedAssets = @{
    gpl = @{
        Name = "mpv-dev-x86_64-20260725-git-8c67647b50.7z"
        Sha256 = "8d953b7a69f33595b8026779f0f845c9cbb6ba2541e563921f5d176e2ab14965"
    }
    lgpl = @{
        Name = "mpv-dev-lgpl-x86_64-20260725-git-8c67647b50.7z"
        Sha256 = "1f989b6902278b1bb972624ae0849e92bcb9350f7567d93435eac127a8a60641"
    }
}

function Find-Executable {
    param([Parameter(Mandatory)][string]$Name)

    $command = Get-Command $Name -ErrorAction SilentlyContinue
    if ($command) {
        return $command.Source
    }

    if ($Name -eq "7z.exe") {
        $known7Zip = Join-Path $env:ProgramFiles "7-Zip\7z.exe"
        if (Test-Path -LiteralPath $known7Zip) {
            return $known7Zip
        }
    }

    throw "Required tool '$Name' was not found. Install the prerequisite and try again."
}

function Find-MsvcTool {
    param([Parameter(Mandatory)][string]$Name)

    $command = Get-Command $Name -ErrorAction SilentlyContinue
    if ($command) {
        return $command.Source
    }

    $vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
    if (-not (Test-Path -LiteralPath $vswhere)) {
        throw "'$Name' was not found and vswhere.exe is unavailable. Install Visual Studio 2022 with the Desktop development with C++ workload."
    }

    $relativePattern = "VC\Tools\MSVC\**\bin\Hostx64\x64\$Name"
    $result = & $vswhere -latest -products * `
        -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
        -find $relativePattern | Select-Object -First 1
    if (-not $result) {
        throw "'$Name' was not found in the latest Visual Studio C++ installation."
    }

    return $result
}

function Get-LatestAsset {
    param([Parameter(Mandatory)][string]$RequestedVariant)

    $headers = @{
        Accept = "application/vnd.github+json"
        "User-Agent" = "Supernova-fetch-mpv"
    }
    $release = Invoke-RestMethod `
        -Uri "https://api.github.com/repos/zhongfly/mpv-winbuild/releases/latest" `
        -Headers $headers

    $pattern = if ($RequestedVariant -eq "lgpl") {
        "^mpv-dev-lgpl-x86_64-\d{8}-git-[0-9a-f]+\.7z$"
    } else {
        "^mpv-dev-x86_64-\d{8}-git-[0-9a-f]+\.7z$"
    }

    $assets = @($release.assets | Where-Object { $_.name -match $pattern })
    if ($assets.Count -ne 1) {
        throw "Expected exactly one standard x64 $RequestedVariant development asset in release '$($release.tag_name)', found $($assets.Count)."
    }

    $checksumAsset = $release.assets | Where-Object { $_.name -eq "sha256.txt" } |
        Select-Object -First 1
    if (-not $checksumAsset) {
        throw "Release '$($release.tag_name)' does not publish sha256.txt."
    }

    $checksumResponse = Invoke-WebRequest -Uri $checksumAsset.browser_download_url -Headers $headers
    $checksumText = if ($checksumResponse.Content -is [byte[]]) {
        [Text.Encoding]::UTF8.GetString($checksumResponse.Content)
    } else {
        [string]$checksumResponse.Content
    }
    $checksumLine = ($checksumText -split "\r?\n") |
        Where-Object { $_ -match "^(?<hash>[0-9a-fA-F]{64})\s+\*?$([regex]::Escape($assets[0].name))$" } |
        Select-Object -First 1
    if (-not $checksumLine) {
        throw "No published SHA-256 entry was found for '$($assets[0].name)'."
    }

    $hash = [regex]::Match($checksumLine, "^[0-9a-fA-F]{64}").Value.ToLowerInvariant()
    return @{
        Tag = $release.tag_name
        Name = $assets[0].name
        Url = $assets[0].browser_download_url
        Sha256 = $hash
    }
}

function New-DefinitionFile {
    param(
        [Parameter(Mandatory)][string]$DllPath,
        [Parameter(Mandatory)][string]$OutputPath,
        [Parameter(Mandatory)][string]$DumpbinPath
    )

    $dumpOutput = & $DumpbinPath /nologo /exports $DllPath
    if ($LASTEXITCODE -ne 0) {
        throw "dumpbin failed while reading exports from '$DllPath'."
    }

    $exports = foreach ($line in $dumpOutput) {
        if ($line -match "^\s+\d+\s+[0-9A-Fa-f]+\s+[0-9A-Fa-f]+\s+(?<name>\S+)") {
            $Matches.name
        }
    }
    $exports = @($exports | Sort-Object -Unique)
    if ($exports.Count -eq 0) {
        throw "No exported symbols were discovered in '$DllPath'."
    }

    $definition = @(
        "LIBRARY `"$([IO.Path]::GetFileName($DllPath))`""
        "EXPORTS"
    ) + ($exports | ForEach-Object { "    $_" })
    Set-Content -LiteralPath $OutputPath -Value $definition -Encoding ascii
}

$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot ".."))
$destination = [IO.Path]::GetFullPath((Join-Path $repoRoot "deps\mpv"))
$expectedDestination = [IO.Path]::GetFullPath((Join-Path $repoRoot "deps\mpv"))
if ($destination -ne $expectedDestination -or [IO.Path]::GetFileName($destination) -ne "mpv") {
    throw "Refusing to replace an unexpected destination: '$destination'."
}

$sevenZip = Find-Executable "7z.exe"
$dumpbin = Find-MsvcTool "dumpbin.exe"
$libTool = Find-MsvcTool "lib.exe"

if ($Latest) {
    $asset = Get-LatestAsset $Variant
} else {
    $pinned = $PinnedAssets[$Variant]
    $asset = @{
        Tag = $PinnedTag
        Name = $pinned.Name
        Url = "https://github.com/zhongfly/mpv-winbuild/releases/download/$PinnedTag/$($pinned.Name)"
        Sha256 = $pinned.Sha256
    }
}

$tempRoot = Join-Path ([IO.Path]::GetTempPath()) ("supernova-mpv-" + [guid]::NewGuid().ToString("N"))
$archivePath = Join-Path $tempRoot $asset.Name
$extractRoot = Join-Path $tempRoot "extracted"

try {
    New-Item -ItemType Directory -Path $extractRoot -Force | Out-Null
    Write-Host "Downloading $($asset.Name) from release $($asset.Tag)..."
    Invoke-WebRequest -Uri $asset.Url -OutFile $archivePath -Headers @{
        "User-Agent" = "Supernova-fetch-mpv"
    }

    $actualHash = (Get-FileHash -LiteralPath $archivePath -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($actualHash -ne $asset.Sha256.ToLowerInvariant()) {
        throw "SHA-256 mismatch for '$($asset.Name)'. Expected $($asset.Sha256), got $actualHash."
    }

    & $sevenZip x $archivePath "-o$extractRoot" -y | Out-Host
    if ($LASTEXITCODE -ne 0) {
        throw "7-Zip failed to extract '$archivePath'."
    }

    $clientHeader = Get-ChildItem -LiteralPath $extractRoot -Recurse -File -Filter client.h |
        Where-Object { $_.Directory.Name -eq "mpv" } | Select-Object -First 1
    $runtimeDlls = @(Get-ChildItem -LiteralPath $extractRoot -Recurse -File |
        Where-Object { $_.Name -in @("libmpv-2.dll", "mpv-2.dll") })
    if (-not $clientHeader) {
        throw "The archive does not contain include/mpv/client.h."
    }
    if ($runtimeDlls.Count -ne 1) {
        throw "Expected exactly one libmpv runtime DLL, found $($runtimeDlls.Count)."
    }

    if (Test-Path -LiteralPath $destination) {
        Remove-Item -LiteralPath $destination -Recurse -Force
    }
    $includeDestination = Join-Path $destination "include\mpv"
    $libDestination = Join-Path $destination "lib"
    $binDestination = Join-Path $destination "bin"
    New-Item -ItemType Directory -Path $includeDestination, $libDestination, $binDestination -Force | Out-Null

    Copy-Item -Path (Join-Path $clientHeader.Directory.FullName "*") `
        -Destination $includeDestination -Recurse -Force
    $dllDestination = Join-Path $binDestination $runtimeDlls[0].Name
    Copy-Item -LiteralPath $runtimeDlls[0].FullName -Destination $dllDestination -Force

    $definitionSource = Get-ChildItem -LiteralPath $extractRoot -Recurse -File -Filter *.def |
        Select-Object -First 1
    $definitionPath = Join-Path $libDestination "mpv.def"
    if ($definitionSource) {
        Copy-Item -LiteralPath $definitionSource.FullName -Destination $definitionPath -Force
    } else {
        New-DefinitionFile -DllPath $dllDestination -OutputPath $definitionPath -DumpbinPath $dumpbin
    }

    $importLibrary = Join-Path $libDestination "mpv.lib"
    & $libTool /nologo "/def:$definitionPath" "/name:$($runtimeDlls[0].Name)" `
        "/out:$importLibrary" /machine:x64
    if ($LASTEXITCODE -ne 0 -or -not (Test-Path -LiteralPath $importLibrary)) {
        throw "lib.exe failed to generate '$importLibrary'."
    }

    $requiredFiles = @(
        (Join-Path $includeDestination "client.h")
        (Join-Path $includeDestination "render.h")
        $dllDestination
        $importLibrary
    )
    foreach ($requiredFile in $requiredFiles) {
        if (-not (Test-Path -LiteralPath $requiredFile)) {
            throw "Required normalized file is missing: '$requiredFile'."
        }
    }

    $metadata = [ordered]@{
        release = $asset.Tag
        asset = $asset.Name
        sha256 = $asset.Sha256.ToLowerInvariant()
        variant = $Variant
    }
    $metadata | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $destination "metadata.json") -Encoding utf8
    Write-Host "libmpv $Variant development package is ready under '$destination'."
} finally {
    if (Test-Path -LiteralPath $tempRoot) {
        Remove-Item -LiteralPath $tempRoot -Recurse -Force
    }
}
