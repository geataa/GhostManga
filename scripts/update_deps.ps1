<#
.SYNOPSIS
    Audits GhostManga's vendored third-party libraries against upstream releases
    and updates QuickJS-NG (quickjs-amalgam) and Microsoft.Web.WebView2 SDK.

.EXAMPLE
    powershell -ExecutionPolicy Bypass -File scripts\update_deps.ps1
    powershell -ExecutionPolicy Bypass -File scripts\update_deps.ps1 -CheckOnly
#>
param(
    [switch]$CheckOnly
)

$ErrorActionPreference = "Stop"
$RepoRoot = Split-Path -Parent $PSScriptRoot

Write-Host "=== GhostManga Dependency & Evergreen Component Audit ===" -ForegroundColor Cyan

function Get-LatestGitHubRelease([string]$Repo) {
    try {
        return Invoke-RestMethod -Uri "https://api.github.com/repos/$Repo/releases/latest" -Headers @{ "User-Agent" = "GhostManga-UpdateDeps" } -TimeoutSec 10
    } catch {
        return $null
    }
}

# 1. QuickJS-NG
$qjsHeader = Join-Path $RepoRoot "third_party\quickjs\quickjs.h"
$qjsLocal = if (Test-Path $qjsHeader) {
    $m = Select-String -Path $qjsHeader -Pattern 'QJS_VERSION_(MAJOR|MINOR|PATCH)\s+(\d+)'
    if ($m.Count -ge 3) { "$($m[0].Matches[0].Groups[2].Value).$($m[1].Matches[0].Groups[2].Value).$($m[2].Matches[0].Groups[2].Value)" } else { "installed" }
} else { "missing" }
$qjsRel = Get-LatestGitHubRelease "quickjs-ng/quickjs"
$qjsRemote = if ($qjsRel) { $qjsRel.tag_name } else { "(offline)" }
Write-Host ("  [QuickJS-NG]  Local: {0,-14} Latest GitHub Release: {1}" -f $qjsLocal, $qjsRemote)

# 2. Lexbor
$lxbVerHeader = Get-ChildItem -Path (Join-Path $RepoRoot "third_party\lexbor") -Filter "version.h" -Recurse -ErrorAction SilentlyContinue | Select-Object -First 1
$lxbLocal = if ($lxbVerHeader) {
    $m = Select-String -Path $lxbVerHeader.FullName -Pattern 'LEXBOR_VERSION_STRING\s+"([^"]+)"'
    if ($m) { $m[0].Matches[0].Groups[1].Value } else { "installed" }
} else { "installed" }
$lxbRel = Get-LatestGitHubRelease "lexbor/lexbor"
$lxbRemote = if ($lxbRel) { $lxbRel.tag_name } else { "(offline)" }
Write-Host ("  [Lexbor]      Local: {0,-14} Latest GitHub Release: {1}" -f $lxbLocal, $lxbRemote)

# 3. Miniz
$mzHeader = Join-Path $RepoRoot "third_party\miniz\miniz.h"
$mzLocal = if (Test-Path $mzHeader) {
    $m = Select-String -Path $mzHeader -Pattern 'MZ_VERSION\s+"([^"]+)"'
    if ($m) { $m[0].Matches[0].Groups[1].Value } else { "installed" }
} else { "missing" }
$mzRel = Get-LatestGitHubRelease "richgel999/miniz"
$mzRemote = if ($mzRel) { $mzRel.tag_name } else { "(offline)" }
Write-Host ("  [Miniz]       Local: {0,-14} Latest GitHub Release: {1}" -f $mzLocal, $mzRemote)

# 4. Microsoft.Web.WebView2 NuGet SDK
$nugetIdx = Invoke-RestMethod -Uri "https://api.nuget.org/v3-flatcontainer/microsoft.web.webview2/index.json" -TimeoutSec 15
$stableVersions = $nugetIdx.versions | Where-Object { $_ -notmatch '-' }
$latestWv2 = $stableVersions[-1]
Write-Host ("  [WebView2]    Latest Stable NuGet SDK: {0}" -f $latestWv2)

if ($CheckOnly) {
    Write-Host "`nCheckOnly mode finished. Run without -CheckOnly to update QuickJS-NG and WebView2 SDK." -ForegroundColor Green
    exit 0
}

$tmpDir = Join-Path ([System.IO.Path]::GetTempPath()) ("gm_deps_" + [Guid]::NewGuid().ToString("N"))
New-Item -ItemType Directory -Path $tmpDir | Out-Null
try {
    # Update QuickJS-NG amalgam if asset available
    if ($qjsRel) {
        $amalgamAsset = $qjsRel.assets | Where-Object { $_.name -eq "quickjs-amalgam.zip" } | Select-Object -First 1
        if ($amalgamAsset) {
            $qjsZip = Join-Path $tmpDir "quickjs-amalgam.zip"
            $qjsExt = Join-Path $tmpDir "qjs_ext"
            Write-Host "`nDownloading QuickJS-NG $($qjsRel.tag_name) amalgam..." -ForegroundColor Yellow
            Invoke-WebRequest -Uri $amalgamAsset.browser_download_url -OutFile $qjsZip -UseBasicParsing
            Expand-Archive -Path $qjsZip -DestinationPath $qjsExt -Force
            Copy-Item -Path (Join-Path $qjsExt "*") -Destination (Join-Path $RepoRoot "third_party\quickjs") -Force
            $qjsObj = Join-Path $RepoRoot "build\obj\quickjs.obj"
            if (Test-Path $qjsObj) { Remove-Item $qjsObj -Force }
            Write-Host "Updated third_party\quickjs to $($qjsRel.tag_name)!" -ForegroundColor Green
        }
    }

    # Update WebView2 SDK
    $nupkgUrl = "https://api.nuget.org/v3-flatcontainer/microsoft.web.webview2/$latestWv2/microsoft.web.webview2.$latestWv2.nupkg"
    $zipPath = Join-Path $tmpDir "webview2.zip"
    Write-Host "`nDownloading Microsoft.Web.WebView2 $latestWv2..." -ForegroundColor Yellow
    Invoke-WebRequest -Uri $nupkgUrl -OutFile $zipPath -UseBasicParsing
    $extractDir = Join-Path $tmpDir "wv2_ext"
    Expand-Archive -Path $zipPath -DestinationPath $extractDir -Force

    $incSrc = Join-Path $extractDir "build\native\include"
    $libSrc = Join-Path $extractDir "build\native\x64\WebView2LoaderStatic.lib"
    $incDst = Join-Path $RepoRoot "third_party\webview2\include"
    $libDst = Join-Path $RepoRoot "third_party\webview2\lib\WebView2LoaderStatic.lib"

    Copy-Item -Path (Join-Path $incSrc "WebView2.h") -Destination (Join-Path $incDst "WebView2.h") -Force
    Copy-Item -Path (Join-Path $incSrc "WebView2EnvironmentOptions.h") -Destination (Join-Path $incDst "WebView2EnvironmentOptions.h") -Force
    Copy-Item -Path $libSrc -Destination $libDst -Force

    Write-Host "Updated third_party\webview2 to Microsoft.Web.WebView2 $latestWv2!" -ForegroundColor Green
} finally {
    if (Test-Path $tmpDir) {
        Remove-Item -Path $tmpDir -Recurse -Force -ErrorAction SilentlyContinue
    }
}
