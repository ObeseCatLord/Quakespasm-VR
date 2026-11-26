# Collect the win-x64 runtime from an MSVC Release build into the exact file set
# the release packages ship, and write windows-artifact-manifest.json beside it.
param(
  [Parameter(Mandatory)] [string] $Source,
  [Parameter(Mandatory)] [string] $Destination,
  [Parameter(Mandatory)] [string] $Revision
)
$ErrorActionPreference = 'Stop'
$runtime = @(
  'vkQuake.exe', 'SDL3.dll', 'phonon.dll', 'libcurl.dll', 'nghttp2.dll', 'z.dll',
  'libcrypto-3-x64.dll', 'libssl-3-x64.dll', 'libFLAC-8.dll', 'libmad-0.dll', 'libmikmod-3.dll',
  'libmpg123-0.dll', 'libogg-0.dll', 'libopus-0.dll', 'libopusfile-0.dll', 'libvorbis-0.dll',
  'libvorbisfile-3.dll', 'libxmp.dll', 'LICENSE.opus.txt'
)
if ($Revision -notmatch '^[0-9a-f]{40}$') { throw "Revision must be a 40-digit commit id" }
$release = Join-Path $Destination 'Release'
New-Item -ItemType Directory -Force $release | Out-Null
foreach ($name in $runtime) {
  $path = Join-Path $Source $name
  if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { throw "Build output is missing $name" }
  Copy-Item -LiteralPath $path -Destination $release
}
Copy-Item -LiteralPath (Join-Path $PSScriptRoot '..\..\LICENSE.txt') -Destination $release
$files = Get-ChildItem -File $release | Sort-Object Name | ForEach-Object {
  [ordered]@{ file = $_.Name; sha256 = (Get-FileHash -Algorithm SHA256 $_.FullName).Hash.ToLower() }
}
$manifest = [ordered]@{
  revision = $Revision
  builder = 'github-actions'
  run_id = $env:GITHUB_RUN_ID
  files = @($files)
}
$json = $manifest | ConvertTo-Json -Depth 4
[IO.File]::WriteAllText((Join-Path $Destination 'windows-artifact-manifest.json'), $json, [Text.UTF8Encoding]::new($false))
Write-Host "Packaged $($files.Count) files for $Revision"
