$ErrorActionPreference='Stop'
$ProgressPreference='SilentlyContinue'
$taskRoot=Split-Path -Parent $MyInvocation.MyCommand.Path
$source=Join-Path $taskRoot 'source'
$receipts=Join-Path $taskRoot 'receipts'
$artifacts=Join-Path $taskRoot 'artifacts'
$entry=Get-Content -Raw (Join-Path $taskRoot 'entry.json') | ConvertFrom-Json
$reference=Get-Content -Raw (Join-Path $taskRoot 'dependency-reference.json') | ConvertFrom-Json
function SaveJson([string]$path,[object]$value) {
    $body=ConvertTo-Json -InputObject $value -Depth 18
    [IO.File]::WriteAllText($path,$body,[Text.UTF8Encoding]::new($false))
}
$receiptDirExists=Test-Path $receipts
if((Test-Path $source) -or (Test-Path $artifacts)) { throw 'Refusing pre-existing build inputs or artifacts in fresh namespace' }
if($receiptDirExists -and @(Get-ChildItem -LiteralPath $receipts -Force).Count -gt 0) { throw 'Refusing non-empty receipts in fresh namespace' }
if(-not $receiptDirExists) { New-Item -ItemType Directory -Path $receipts | Out-Null }
$prepFailure=$null
try {
    $archive=Join-Path $taskRoot 'product.tar'
    $archiveHash=(Get-FileHash -Algorithm SHA256 $archive).Hash.ToLowerInvariant()
    if($archiveHash -ne $entry.archive_sha256) { throw "Production archive hash mismatch: $archiveHash" }
    if($entry.production_revision -notmatch '^[0-9a-f]{40}$' -or $entry.archive_sha256 -notmatch '^[0-9a-f]{64}$') { throw 'Invalid source identity' }
    if((Get-Item -LiteralPath $archive).Length -ne $entry.archive_bytes) { throw 'Archive size mismatch' }
    $manifest=Get-Content -Raw (Join-Path $taskRoot 'production-source-manifest.json') | ConvertFrom-Json
    if($manifest.archive_sha256 -ne $archiveHash -or $manifest.revision -ne $entry.production_revision) { throw 'Source manifest does not match the production archive/revision' }
    $shaderPlan=Get-Content -Raw (Join-Path $taskRoot 'shader-build-plan.json') | ConvertFrom-Json
    New-Item -ItemType Directory -Path $source | Out-Null
    & tar.exe -xf $archive -C $source
    if($LASTEXITCODE -ne 0) { throw "Exact production archive extraction failed: $LASTEXITCODE" }
    $actualFiles=@(Get-ChildItem -LiteralPath $source -File -Recurse)
    if($actualFiles.Count -ne @($manifest.files).Count) { throw "Extracted source file count mismatch: $($actualFiles.Count) vs $(@($manifest.files).Count)" }
    $expected=@{}
    foreach($f in $manifest.files) { $expected[$f.path]=$f.sha256 }
    $badFiles=@()
    foreach($f in $actualFiles) {
        $rel=$f.FullName.Substring($source.Length+1).Replace('\','/')
        if(-not $expected.ContainsKey($rel)) { $badFiles+=@("unexpected:$rel"); continue }
        $h=(Get-FileHash -Algorithm SHA256 -LiteralPath $f.FullName).Hash.ToLowerInvariant()
        if($h -ne $expected[$rel]) { $badFiles+=@("hash:$rel") }
    }
    if($badFiles.Count) { throw "Extracted source differs from exact manifest: $($badFiles -join ',')" }
    $depManifest=Get-Content -Raw (Join-Path $taskRoot 'dependency-input-manifest.json') | ConvertFrom-Json
    $depCount=0
    foreach($f in $depManifest.files) {
        $p=Join-Path $source ($f.path.Replace('/','\'))
        if(-not(Test-Path -LiteralPath $p -PathType Leaf)) { throw "Pinned dependency input missing from source archive: $($f.path)" }
        if((Get-FileHash -Algorithm SHA256 -LiteralPath $p).Hash.ToLowerInvariant() -ne $f.sha256) { throw "Pinned dependency hash mismatch: $($f.path)" }
        $depCount++
    }
    $forbiddenExt=@('.obj','.pch','.tlog','.pdb','.ilk','.iobj','.ipch','.spv','.spirv')
    $forbidden=@(Get-ChildItem -LiteralPath $source -File -Recurse | Where-Object {$forbiddenExt -contains $_.Extension.ToLowerInvariant()} | ForEach-Object {$_.FullName.Substring($source.Length+1)})
    if($forbidden.Count) { throw "Production archive contains forbidden cached build outputs: $($forbidden -join ',')" }
    $generatedBeforeBuild=@()
    foreach($relative in @($shaderPlan.generated_c_expected)+@($shaderPlan.generated_spv_expected)+@($shaderPlan.expected_shader_objects)) {
        $p=Join-Path $source ($relative.Replace('/','\'))
        if(Test-Path -LiteralPath $p) { $generatedBeforeBuild+=@($relative) }
    }
    if($generatedBeforeBuild.Count) { throw "Fresh archive unexpectedly contains generated shader inputs/objects: $($generatedBeforeBuild -join ',')" }
    $expectedMissing=@()
    $expectedPaths=@(
      (Join-Path $source 'Windows\VisualStudio\vkquake.sln'),
      (Join-Path $source 'Windows\VisualStudio\vkquake.vcxproj'),
      (Join-Path $source 'Quake\gl_model.c'),
      (Join-Path $source 'Windows\SDL3\include\SDL3\SDL.h'),
      (Join-Path $source 'Windows\SDL3\lib64\SDL3.lib'),
      (Join-Path $source 'Windows\codecs\include\opus\opus.h'),
      (Join-Path $source 'Windows\codecs\x64\libopus.lib'),
      (Join-Path $source 'Windows\misc\include\vulkan\vulkan.h'),
      (Join-Path $source 'Windows\misc\x64\vulkan-1.lib')
    )
    foreach($p in $expectedPaths) { if(-not(Test-Path -LiteralPath $p -PathType Leaf)) { $expectedMissing+=@($p) } }
    if($expectedMissing.Count) { throw "Required archived source/dependency inputs missing: $($expectedMissing -join ',')" }
    $taskMSBuild=$reference.msbuild
    $taskToolBin=Join-Path $reference.msvc_toolset_root 'bin\Hostx64\x64'
    $steam=$reference.steam_audio_sdk_root
    $sdk=$reference.vulkan_sdk_root
    $required=@(
      $taskMSBuild,
      (Join-Path $taskToolBin 'cl.exe'),(Join-Path $taskToolBin 'c2.dll'),(Join-Path $taskToolBin 'link.exe'),(Join-Path $taskToolBin 'lib.exe'),(Join-Path $taskToolBin 'dumpbin.exe'),
      (Join-Path $steam 'include\phonon.h'),(Join-Path $steam 'include\phonon_interfaces.h'),(Join-Path $steam 'include\phonon_version.h'),(Join-Path $steam 'lib\windows-x64\phonon.lib'),(Join-Path $steam 'lib\windows-x64\phonon.dll'),
      (Join-Path $sdk 'bin\glslangValidator.exe'),(Join-Path $sdk 'bin\spirv-opt.exe')
    )
    $missing=@(); $externalFacts=@()
    foreach($p in $required) {
      if(-not(Test-Path -LiteralPath $p -PathType Leaf)) { $missing+=@($p); continue }
      $item=Get-Item -LiteralPath $p
      if(-not $reference.external_sha256.PSObject.Properties[$p] -or (Get-FileHash -Algorithm SHA256 -LiteralPath $p).Hash.ToLowerInvariant() -ne $reference.external_sha256.PSObject.Properties[$p].Value) { throw "Pinned external prerequisite hash mismatch: $p" }
      $externalFacts+=@{path=$p;bytes=$item.Length;file_version=$item.VersionInfo.FileVersion;sha256=(Get-FileHash -Algorithm SHA256 -LiteralPath $p).Hash.ToLowerInvariant()}
    }
    if($missing.Count) { throw "Missing pinned dependency/tool prerequisite(s); no installation attempted: $($missing -join ',')" }
    $startMemory=$null
    try { $os=Get-CimInstance Win32_OperatingSystem; $startMemory=@{total_physical_kb=[int64]$os.TotalVisibleMemorySize;available_physical_kb=[int64]$os.FreePhysicalMemory;pagefile_usage=@(Get-CimInstance Win32_PageFileUsage -ErrorAction SilentlyContinue | ForEach-Object {@{name=$_.Name;allocated_kb=$_.AllocatedBaseSize;current_kb=$_.CurrentUsage;peak_kb=$_.PeakUsage}})} } catch {}
    $buildOut=Join-Path $source 'Windows\VisualStudio\Build-vkQuake\x64\Release'
    if(Test-Path $buildOut) { throw "Fresh Release output path unexpectedly exists in extracted source: $buildOut" }
    $prep=@{
      status='ready';revision=$entry.production_revision;archive_sha256=$archiveHash;archive_bytes=(Get-Item $archive).Length;
      extracted_file_count=$actualFiles.Count;source_hash_manifest=(Join-Path $taskRoot 'production-source-manifest.json');
      dependency_manifest=(Join-Path $taskRoot 'dependency-input-manifest.json');pinned_archive_dependencies_verified=$depCount;
      external_dependency_and_tool_facts=$externalFacts;
      prior_cache_not_copied=$true;forbidden_cache_file_count=$forbidden.Count;release_output_absent_before_build=$true;
      initially_empty_receipt_directory_allowed=$receiptDirExists;
      release_configuration='Release';platform='x64';platform_toolset='v143';preferred_tool_architecture='x64';
      available_memory_start=$startMemory;runtime_executed=$false;install_performed=$false
    }
    Copy-Item (Join-Path $taskRoot 'production-source-manifest.json') (Join-Path $receipts 'production-source-manifest.json')
    Copy-Item (Join-Path $taskRoot 'dependency-input-manifest.json') (Join-Path $receipts 'dependency-input-manifest.json')
    Copy-Item (Join-Path $taskRoot 'shader-build-plan.json') (Join-Path $receipts 'shader-build-plan.json')
    SaveJson (Join-Path $receipts 'preparation.json') $prep
    Write-Output "PREPARED: current source and pinned dependencies verified; no cached build outputs present"
} catch {
    $prepFailure=$_.Exception.ToString()
    try { [IO.File]::WriteAllText((Join-Path $receipts 'preparation-error.txt'),$prepFailure,[Text.UTF8Encoding]::new($false)) } catch {}
    try { SaveJson (Join-Path $receipts 'preparation-status.json') @{status='failed';error=$prepFailure;revision=$entry.production_revision;archive_sha256=$entry.archive_sha256;install_performed=$false} } catch {}
    Write-Error $prepFailure
    exit 2
}
