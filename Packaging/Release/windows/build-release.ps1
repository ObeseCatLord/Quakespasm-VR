$ErrorActionPreference='Stop'
$ProgressPreference='SilentlyContinue'
$taskRoot=Split-Path -Parent $MyInvocation.MyCommand.Path
$source=Join-Path $taskRoot 'source'
$receipts=Join-Path $taskRoot 'receipts'
$artifactRoot=Join-Path $taskRoot 'artifacts'
$entry=Get-Content -Raw (Join-Path $taskRoot 'entry.json') | ConvertFrom-Json
$reference=Get-Content -Raw (Join-Path $taskRoot 'dependency-reference.json') | ConvertFrom-Json
$preparation=Get-Content -Raw (Join-Path $receipts 'preparation.json') | ConvertFrom-Json
$shaderPlan=Get-Content -Raw (Join-Path $taskRoot 'shader-build-plan.json') | ConvertFrom-Json
$taskMSBuild=$reference.msbuild
$taskToolBin=Join-Path $reference.msvc_toolset_root 'bin\Hostx64\x64'
$taskSteam=$reference.steam_audio_sdk_root
$taskSdk=$reference.vulkan_sdk_root
$taskSolution=Join-Path $source 'Windows\VisualStudio\vkquake.sln'
$taskOut=Join-Path $source 'Windows\VisualStudio\Build-vkQuake\x64\Release'
$nativeStatusPath=Join-Path $receipts 'native-msbuild-status.json'
if($preparation.status -ne 'ready') { throw 'Fresh archive/dependency preparation receipt is not ready' }
if(Test-Path $nativeStatusPath) { throw 'Native MSBuild status already exists; refusing a second build' }
if(Test-Path $taskOut) { throw 'Native Release output exists before the fresh build; refusing cache reuse' }
if(Test-Path $artifactRoot) { throw 'Artifact root already exists in fresh namespace' }
$env:VULKAN_SDK=$taskSdk
$taskArgs=@(
  $taskSolution,
  '/p:Configuration=Release',
  '/p:Platform=x64',
  '/p:PlatformToolset=v143',
  '/p:PreferredToolArchitecture=x64',
  "/p:SteamAudioSdkDir=$taskSteam",
  '/p:CL_MPCount=1',
  '/m:1',
  '/v:diag',
  ('/bl:'+(Join-Path $receipts 'Release.binlog')),
  '/nologo',
  '/nr:false'
)
$argv=@($taskMSBuild)+$taskArgs
($argv | ConvertTo-Json -Depth 4) | Set-Content -Encoding UTF8 (Join-Path $receipts 'native-msbuild-argv.json')
$memoryBefore=$null
try {
  $os=Get-CimInstance Win32_OperatingSystem
  $memoryBefore=@{total_physical_kb=[int64]$os.TotalVisibleMemorySize;available_physical_kb=[int64]$os.FreePhysicalMemory;pagefile_usage=@(Get-CimInstance Win32_PageFileUsage -ErrorAction SilentlyContinue | ForEach-Object {@{name=$_.Name;allocated_kb=$_.AllocatedBaseSize;current_kb=$_.CurrentUsage;peak_kb=$_.PeakUsage}})}
} catch {}
$startUtc=[DateTime]::UtcNow
$nativeExit=$null
$launchFailure=$null
$processId=$null
$nativeStartUtc=$null
$nativeEndUtc=$null
$processPeakWorkingSet=$null
try {
  $quoted=($taskArgs | ForEach-Object {'"'+$_+'"'}) -join ' '
  $proc=Start-Process -FilePath $taskMSBuild -ArgumentList $quoted -WorkingDirectory (Split-Path $taskSolution) -PassThru -RedirectStandardOutput (Join-Path $receipts 'Release.log') -RedirectStandardError (Join-Path $receipts 'Release-error.log')
  [void]$proc.Handle
  $processId=[int]$proc.Id
  $nativeStartUtc=$proc.StartTime.ToUniversalTime().ToString('o')
  [IO.File]::WriteAllText((Join-Path $receipts 'native-msbuild-pid.json'),(ConvertTo-Json -InputObject @{pid=$processId;start_utc=$nativeStartUtc;configuration='Release';platform='x64';revision=$entry.production_revision;archive_sha256=$entry.archive_sha256} -Depth 4),[Text.UTF8Encoding]::new($false))
  $proc.WaitForExit()
  $nativeExit=[int]$proc.ExitCode
  $nativeEndUtc=[DateTime]::UtcNow.ToString('o')
  try {$processPeakWorkingSet=[int64]$proc.PeakWorkingSet64} catch {}
} catch {
  $launchFailure=$_.Exception.ToString()
  try { if($proc -and $proc.HasExited) {$nativeExit=[int]$proc.ExitCode} } catch {}
  $nativeEndUtc=[DateTime]::UtcNow.ToString('o')
}
$nativeResult=@{
  configuration='Release';platform='x64';platform_toolset='v143';preferred_tool_architecture='x64';
  msbuild_executable=$taskMSBuild;argv=$argv;parallel_jobs=1;cl_mp_count=1;
  optimization_policy='Project Release configuration unchanged; /O2 /GL and LTCG retained';
  start_utc=$startUtc.ToString('o');native_start_utc=$nativeStartUtc;native_end_utc=$nativeEndUtc;
  process_id=$processId;native_exit_code=$nativeExit;launcher_exception=$launchFailure;process_peak_working_set_bytes=$processPeakWorkingSet;
  revision=$entry.production_revision;archive_sha256=$entry.archive_sha256;
  source_manifest_sha256=(Get-FileHash -Algorithm SHA256 (Join-Path $receipts 'production-source-manifest.json')).Hash.ToLowerInvariant();
  dependency_manifest_sha256=(Get-FileHash -Algorithm SHA256 (Join-Path $receipts 'dependency-input-manifest.json')).Hash.ToLowerInvariant();
  memory_before=$memoryBefore;runtime_executed=$false
}
try {
  [IO.File]::WriteAllText($nativeStatusPath,(ConvertTo-Json -InputObject $nativeResult -Depth 12),[Text.UTF8Encoding]::new($false))
} catch {
  try {[IO.File]::WriteAllText((Join-Path $receipts 'native-msbuild-status-fallback.txt'),("native_exit_code="+$nativeExit+[Environment]::NewLine+"process_id="+$processId+[Environment]::NewLine+$_.Exception.ToString()),[Text.UTF8Encoding]::new($false))} catch {}
}
$logPath=Join-Path $receipts 'Release.log'
$logText=''
if(Test-Path $logPath) { try {$logText=[IO.File]::ReadAllText($logPath)} catch {} }
$pathPatterns=@(
  @{name='cl';regex='(?i)([A-Z]:\\[^"\r\n]*\\HostX64\\x64\\cl\.exe)'},
  @{name='link';regex='(?i)([A-Z]:\\[^"\r\n]*\\HostX64\\x64\\link\.exe)'},
  @{name='c2';regex='(?i)([A-Z]:\\[^"\r\n]*\\HostX64\\x64\\c2\.dll)'},
  @{name='glslangValidator';regex='(?i)([A-Z]:\\[^"\r\n]*\\glslangValidator\.exe)'},
  @{name='spirv-opt';regex='(?i)([A-Z]:\\[^"\r\n]*\\spirv-opt\.exe)'}
)
$observedPaths=@{}
foreach($pattern in $pathPatterns) {
  $values=@()
  foreach($m in [regex]::Matches($logText,$pattern.regex)) { if($m.Groups[1].Value -notin $values) {$values+=@($m.Groups[1].Value)} }
  $observedPaths[$pattern.name]=$values
}
$toolFacts=@()
foreach($name in @('cl.exe','c2.dll','link.exe','lib.exe','dumpbin.exe')) {
  $path=Join-Path $taskToolBin $name
  if(Test-Path $path) {$item=Get-Item $path;$toolFacts+=@{name=$name;path=$path;file_version=$item.VersionInfo.FileVersion;sha256=(Get-FileHash -Algorithm SHA256 $path).Hash.ToLowerInvariant()}}
}
$toolReceipt=@{preferred_tool_architecture='x64';observed_paths=$observedPaths;installed_x64_tool_facts=$toolFacts;cl_hostx64_observed=($observedPaths['cl'].Count -gt 0);link_hostx64_observed=($observedPaths['link'].Count -gt 0)}
try {[IO.File]::WriteAllText((Join-Path $receipts 'tool-paths.json'),(ConvertTo-Json -InputObject $toolReceipt -Depth 10),[Text.UTF8Encoding]::new($false))} catch {}
$memoryErrors=@()
foreach($m in [regex]::Matches($logText,'(?im)^.*(?:out of memory|not enough memory|virtual memory|memory allocation failed|C1060|C1076|C3859).*$')) {$memoryErrors+=@($m.Value.Substring(0,[Math]::Min(1200,$m.Value.Length)))}
$errorLines=@()
foreach($m in [regex]::Matches($logText,'(?im)^.*(?:fatal error C\d+|fatal error LNK\d+|error C\d+|error LNK\d+|error MSB\d+|LNK1000|C1001).*$')) {$errorLines+=@($m.Value.Substring(0,[Math]::Min(1400,$m.Value.Length)))}
$memoryAfter=$null
try {$os=Get-CimInstance Win32_OperatingSystem;$memoryAfter=@{total_physical_kb=[int64]$os.TotalVisibleMemorySize;available_physical_kb=[int64]$os.FreePhysicalMemory;pagefile_usage=@(Get-CimInstance Win32_PageFileUsage -ErrorAction SilentlyContinue | ForEach-Object {@{name=$_.Name;allocated_kb=$_.AllocatedBaseSize;current_kb=$_.CurrentUsage;peak_kb=$_.PeakUsage}})}} catch {}
try {
  [IO.File]::WriteAllText((Join-Path $receipts 'memory-errors.json'),(ConvertTo-Json -InputObject @{before=$memoryBefore;after=$memoryAfter;matches=$memoryErrors} -Depth 10),[Text.UTF8Encoding]::new($false))
  [IO.File]::WriteAllText((Join-Path $receipts 'native-errors.json'),(ConvertTo-Json -InputObject @{native_exit_code=$nativeExit;build_log_error_lines=$errorLines;memory_error_count=$memoryErrors.Count} -Depth 8),[Text.UTF8Encoding]::new($false))
} catch {}
$postprocessError=$null
$packageVerified=$false
$verificationExit=0
try {
  $objects=@(Get-ChildItem -LiteralPath $source -Recurse -File -Filter '*.obj' -ErrorAction SilentlyContinue)
  $objectFacts=@()
  $allObjectsFresh=$true
  foreach($f in $objects) {
    $fresh=($f.LastWriteTimeUtc -ge $startUtc.AddSeconds(-2))
    if(-not $fresh) {$allObjectsFresh=$false}
    $objectFacts+=@{path=$f.FullName.Substring($source.Length+1).Replace('\','/');bytes=$f.Length;last_write_utc=$f.LastWriteTimeUtc.ToString('o');sha256=(Get-FileHash -Algorithm SHA256 $f.FullName).Hash.ToLowerInvariant();fresh_since_attempt=$fresh}
  }
  $shaderCResults=@();$shaderCCount=0;$shaderCAllFresh=$true
  foreach($relative in $shaderPlan.generated_c_expected) {
    $p=Join-Path $source ($relative.Replace('/','\'))
    $exists=Test-Path -LiteralPath $p -PathType Leaf
    $fresh=$false;$hash=$null;$bytes=$null
    if($exists) {$f=Get-Item -LiteralPath $p;$fresh=($f.LastWriteTimeUtc -ge $startUtc.AddSeconds(-2));$hash=(Get-FileHash -Algorithm SHA256 $p).Hash.ToLowerInvariant();$bytes=$f.Length}
    if($exists) {$shaderCCount++}
    if(-not ($exists -and $fresh)) {$shaderCAllFresh=$false}
    $shaderCResults+=@{path=$relative;exists=$exists;fresh_since_attempt=$fresh;bytes=$bytes;sha256=$hash}
  }
  $shaderSpvResults=@();$shaderSpvCount=0;$shaderSpvAllFresh=$true
  foreach($relative in $shaderPlan.generated_spv_expected) {
    $p=Join-Path $source ($relative.Replace('/','\'))
    $exists=Test-Path -LiteralPath $p -PathType Leaf
    $fresh=$false;$hash=$null;$bytes=$null
    if($exists) {$f=Get-Item -LiteralPath $p;$fresh=($f.LastWriteTimeUtc -ge $startUtc.AddSeconds(-2));$hash=(Get-FileHash -Algorithm SHA256 $p).Hash.ToLowerInvariant();$bytes=$f.Length}
    if($exists) {$shaderSpvCount++}
    if(-not ($exists -and $fresh)) {$shaderSpvAllFresh=$false}
    $shaderSpvResults+=@{path=$relative;exists=$exists;fresh_since_attempt=$fresh;bytes=$bytes;sha256=$hash}
  }
  $shaderObjectResults=@();$shaderObjectCount=0;$shaderObjectsAllFresh=$true
  foreach($relative in $shaderPlan.expected_shader_objects) {
    $p=Join-Path $source ($relative.Replace('/','\'))
    $exists=Test-Path -LiteralPath $p -PathType Leaf
    $fresh=$false;$hash=$null;$bytes=$null
    if($exists) {$f=Get-Item -LiteralPath $p;$fresh=($f.LastWriteTimeUtc -ge $startUtc.AddSeconds(-2));$hash=(Get-FileHash -Algorithm SHA256 $p).Hash.ToLowerInvariant();$bytes=$f.Length}
    if($exists) {$shaderObjectCount++}
    if(-not ($exists -and $fresh)) {$shaderObjectsAllFresh=$false}
    $shaderObjectResults+=@{path=$relative;exists=$exists;fresh_since_attempt=$fresh;bytes=$bytes;sha256=$hash}
  }
  $compileFacts=@{revision=$entry.production_revision;archive_sha256=$entry.archive_sha256;build_started_utc=$startUtc.ToString('o');archive_regular_file_count=$preparation.extracted_file_count;expected_shader_c_objects=$shaderPlan.compiled_shader_c_input_count;fresh_shader_c_generated=$shaderCCount;all_shader_c_fresh=$shaderCAllFresh;fresh_shader_spv_generated=$shaderSpvCount;all_shader_spv_fresh=$shaderSpvAllFresh;fresh_shader_objects=$shaderObjectCount;all_shader_objects_fresh=$shaderObjectsAllFresh;all_object_count=$objects.Count;all_objects_fresh=$allObjectsFresh;object_files=$objectFacts;shader_c_files=$shaderCResults;shader_spv_files=$shaderSpvResults;shader_object_files=$shaderObjectResults;accepted_object_cache_copied=$false}
  [IO.File]::WriteAllText((Join-Path $receipts 'fresh-compile-inventory.json'),(ConvertTo-Json -InputObject $compileFacts -Depth 12),[Text.UTF8Encoding]::new($false))
  if($nativeExit -eq 0) {
    if(-not $toolReceipt.cl_hostx64_observed -or -not $toolReceipt.link_hostx64_observed) { throw 'Actual HostX64 compiler/linker paths were not recorded' }
    if(-not $allObjectsFresh -or $objects.Count -eq 0) { throw "Fresh native object check failed; count=$($objects.Count), all_fresh=$allObjectsFresh" }
    if($shaderCCount -ne $shaderPlan.compiled_shader_c_input_count -or -not $shaderCAllFresh) { throw "Fresh generated shader C inputs incomplete: $shaderCCount/$($shaderPlan.compiled_shader_c_input_count), all_fresh=$shaderCAllFresh" }
    if($shaderSpvCount -ne $shaderPlan.expected_generated_spv_count -or -not $shaderSpvAllFresh) { throw "Fresh SPIR-V generation incomplete: $shaderSpvCount/$($shaderPlan.expected_generated_spv_count), all_fresh=$shaderSpvAllFresh" }
    if($shaderObjectCount -ne $shaderPlan.compiled_shader_c_input_count -or -not $shaderObjectsAllFresh) { throw "Fresh shader object inputs incomplete: $shaderObjectCount/$($shaderPlan.compiled_shader_c_input_count), all_fresh=$shaderObjectsAllFresh" }
    $exe=Join-Path $taskOut 'vkQuake.exe'
    $pdb=Join-Path $taskOut 'vkQuake.pdb'
    if(-not(Test-Path $exe) -or -not(Test-Path $pdb)) { throw 'Fresh linked engine or symbol PDB is missing' }
    $stage=Join-Path $artifactRoot 'Release'
    New-Item -ItemType Directory -Path $stage | Out-Null
    Get-ChildItem -LiteralPath $taskOut -File | Where-Object {$_.Extension -in @('.exe','.dll') -or $_.Name -in @('LICENSE.opus.txt','vkQuake.pdb')} | Copy-Item -Destination $stage
    Copy-Item (Join-Path $source 'LICENSE.txt') $stage
    $baseHashes=Get-Content -Raw (Join-Path $taskRoot 'dependency-baseline-hashes-0f277d1f.json') | ConvertFrom-Json
    $expected=$baseHashes.Release
    $expectedNames=@($expected.PSObject.Properties.Name | Sort-Object)
    $files=@(Get-ChildItem -LiteralPath $stage -File)
    $actualNames=@($files | ForEach-Object {$_.Name} | Sort-Object)
    if(($expectedNames -join '|') -ne ($actualNames -join '|')) { throw "Fresh Release package filename set differs; expected=$($expectedNames -join ','); actual=$($actualNames -join ',')" }
    $peFacts=@();$fileFacts=@();$peCount=0
    foreach($f in $files) {
      $sha=(Get-FileHash -Algorithm SHA256 $f.FullName).Hash.ToLowerInvariant()
      $kind='license'
      if($f.Extension -in @('.exe','.dll')) {
        $kind='PE';$peCount++
        $headers=(& (Join-Path $taskToolBin 'dumpbin.exe') /headers $f.FullName | Out-String)
        if($LASTEXITCODE -ne 0) { throw "dumpbin /headers failed: $($f.Name)" }
        if($headers -notmatch '8664 machine \(x64\)') { throw "Staged PE is not x64: $($f.Name)" }
        $deps=(& (Join-Path $taskToolBin 'dumpbin.exe') /dependents $f.FullName | Out-String)
        if($LASTEXITCODE -ne 0) { throw "dumpbin /dependents failed: $($f.Name)" }
        if($f.Name -notin @('vkQuake.exe','vkQuake.pdb') -and $sha -ne $expected.PSObject.Properties[$f.Name].Value) { throw "Pinned dependency PE hash mismatch: $($f.Name)" }
        $depNames=@($deps -split '[\r\n]+' | Where-Object {$_ -match '\.dll'})
        $peFacts+=@{file=$f.Name;bytes=$f.Length;sha256=$sha;machine='x64';dependents=$depNames}
      } elseif($f.Name -eq 'vkQuake.pdb') {
        $kind='symbols'
        if($f.LastWriteTimeUtc -lt $startUtc.AddSeconds(-2)) { throw 'PDB is not fresh for this build' }
      } elseif($sha -ne $expected.PSObject.Properties[$f.Name].Value) {
        throw "Pinned license hash mismatch: $($f.Name)"
      }
      $fileFacts+=@{file=$f.Name;bytes=$f.Length;sha256=$sha;kind=$kind;last_write_utc=$f.LastWriteTimeUtc.ToString('o')}
    }
    $expectedPECount=@($expectedNames | Where-Object {$_ -match '(?i)\.(exe|dll)$'}).Count
    if($peCount -ne $expectedPECount -or $files.Count -ne $expectedNames.Count) { throw "Fresh package inventory count mismatch: files=$($files.Count), PE=$peCount" }
    $imports=(& (Join-Path $taskToolBin 'dumpbin.exe') /imports $exe | Out-String)
    if($LASTEXITCODE -ne 0) { throw 'dumpbin /imports failed' }
    foreach($symbol in @('opus_encoder_create','opus_encoder_ctl','opus_encode','opus_encoder_destroy')) { if($imports -notmatch [regex]::Escape($symbol)) { throw "Required Opus import missing: $symbol" } }
    $builtSha=(Get-FileHash -Algorithm SHA256 $exe).Hash.ToLowerInvariant()
    $stagedSha=(Get-FileHash -Algorithm SHA256 (Join-Path $stage 'vkQuake.exe')).Hash.ToLowerInvariant()
    $artifactResult=@{revision=$entry.production_revision;archive_sha256=$entry.archive_sha256;configuration='Release';platform='x64';engine_buildtree_sha256=$builtSha;engine_staged_sha256=$stagedSha;buildtree_equals_staged=($builtSha -eq $stagedSha);pe_count=$peCount;file_count=$files.Count;fresh_object_count=$objects.Count;fresh_shader_C_count=$shaderCCount;fresh_spv_count=$shaderSpvCount;fresh_shader_object_count=$shaderObjectCount;files=$fileFacts;pes=$peFacts;runtime_executed=$false}
    [IO.File]::WriteAllText((Join-Path $receipts 'artifact-manifest.json'),(ConvertTo-Json -InputObject $artifactResult -Depth 14),[Text.UTF8Encoding]::new($false))
    [IO.File]::WriteAllText((Join-Path $receipts 'artifact-freshness.json'),(ConvertTo-Json -InputObject @{revision=$entry.production_revision;archive_sha256=$entry.archive_sha256;engine_buildtree_sha256=$builtSha;engine_staged_sha256=$stagedSha;equal=($builtSha -eq $stagedSha);package_directory=$stage;file_count=$files.Count;pe_count=$peCount;runtime_executed=$false} -Depth 6),[Text.UTF8Encoding]::new($false))
    $packageVerified=$true
  } elseif($null -eq $nativeExit) {
    throw "MSBuild process did not return an exit code: $launchFailure"
  }
} catch {
  $postprocessError=$_.Exception.ToString()
  $verificationExit=4
  try {[IO.File]::WriteAllText((Join-Path $receipts 'postbuild-error.txt'),$postprocessError,[Text.UTF8Encoding]::new($false))} catch {}
}
$final=@{native_exit_code=$nativeExit;launcher_exception=$launchFailure;postbuild_error=$postprocessError;revision=$entry.production_revision;archive_sha256=$entry.archive_sha256;tool_paths_receipt=(Join-Path $receipts 'tool-paths.json');memory_errors_receipt=(Join-Path $receipts 'memory-errors.json');native_errors_receipt=(Join-Path $receipts 'native-errors.json');fresh_compile_inventory=(Join-Path $receipts 'fresh-compile-inventory.json');artifact_manifest=(Join-Path $receipts 'artifact-manifest.json');package_staged=(Test-Path (Join-Path $artifactRoot 'Release'));package_verified=$packageVerified;runtime_executed=$false}
try {[IO.File]::WriteAllText((Join-Path $receipts 'final-status.json'),(ConvertTo-Json -InputObject $final -Depth 8),[Text.UTF8Encoding]::new($false))} catch {try {[IO.File]::WriteAllText((Join-Path $receipts 'final-status-fallback.txt'),("native_exit_code="+$nativeExit+[Environment]::NewLine+$_.Exception.ToString()),[Text.UTF8Encoding]::new($false))} catch {}}
if($nativeExit -ne $null) {
  if($nativeExit -eq 0 -and $verificationExit -ne 0) {exit $verificationExit}
  exit $nativeExit
}
exit 2
