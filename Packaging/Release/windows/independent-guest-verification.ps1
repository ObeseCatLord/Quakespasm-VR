$ErrorActionPreference='Stop'
$taskRoot=Split-Path -Parent $MyInvocation.MyCommand.Path
$receipts=Join-Path $taskRoot 'receipts'
$source=Join-Path $taskRoot 'source'
$entry=Get-Content -Raw (Join-Path $taskRoot 'entry.json') | ConvertFrom-Json
$manifest=Get-Content -Raw (Join-Path $taskRoot 'production-source-manifest.json') | ConvertFrom-Json
$reference=Get-Content -Raw (Join-Path $taskRoot 'dependency-reference.json') | ConvertFrom-Json
$shaderPlan=Get-Content -Raw (Join-Path $taskRoot 'shader-build-plan.json') | ConvertFrom-Json
$deps=Get-Content -Raw (Join-Path $taskRoot 'dependency-input-manifest.json') | ConvertFrom-Json
$final=Get-Content -Raw (Join-Path $receipts 'final-status.json') | ConvertFrom-Json
$inv=Get-Content -Raw (Join-Path $receipts 'fresh-compile-inventory.json') | ConvertFrom-Json
$art=Get-Content -Raw (Join-Path $receipts 'artifact-manifest.json') | ConvertFrom-Json
$tool=Get-Content -Raw (Join-Path $receipts 'tool-paths.json') | ConvertFrom-Json
function CheckHash([string]$p,[string]$expected){
 if(-not(Test-Path -LiteralPath $p -PathType Leaf)){throw "Missing verification input: $p"}
 $hash=(Get-FileHash -Algorithm SHA256 -LiteralPath $p).Hash.ToLowerInvariant()
 if($hash -ne $expected){throw "Independent hash mismatch: $p"}
}
if($final.native_exit_code -ne 0 -or -not $final.package_verified -or $final.postbuild_error){throw 'Final native build verification did not pass'}
if($inv.all_object_count -ne $reference.expected_native_object_count -or $inv.fresh_shader_objects -ne $shaderPlan.compiled_shader_c_input_count -or -not $inv.all_objects_fresh -or -not $inv.all_shader_objects_fresh){throw 'Unexpected fresh object inventory'}
foreach($r in @($manifest,$final,$inv,$art)){if($r.revision -ne $entry.production_revision -or $r.archive_sha256 -ne $entry.archive_sha256){throw 'Receipt source identity mismatch'}}
CheckHash (Join-Path $taskRoot 'product.tar') $entry.archive_sha256
$count=0
foreach($f in $manifest.files){CheckHash (Join-Path $source ($f.path.Replace('/','\'))) $f.sha256; $count++}
if($count -ne @($manifest.files).Count){throw 'Full source manifest count mismatch'}
$depCount=0
foreach($f in $deps.files){CheckHash (Join-Path $source ($f.path.Replace('/','\'))) $f.sha256; $depCount++}
if($depCount -ne @($deps.files).Count){throw 'Dependency count mismatch'}
$compileCount=0
foreach($set in @($inv.object_files,$inv.shader_c_files,$inv.shader_spv_files,$inv.shader_object_files)){
 foreach($f in $set){CheckHash (Join-Path $source ($f.path.Replace('/','\'))) $f.sha256; $compileCount++}
}
if($compileCount -ne (@($inv.object_files).Count + @($inv.shader_c_files).Count + @($inv.shader_spv_files).Count + @($inv.shader_object_files).Count)){throw "Compile hash count mismatch: $compileCount"}
foreach($f in $art.files){CheckHash (Join-Path $taskRoot ('artifacts/Release/'+$f.file)) $f.sha256}
foreach($p in $reference.external_sha256.PSObject.Properties){CheckHash $p.Name $p.Value}
foreach($f in $tool.installed_x64_tool_facts){CheckHash $f.path $f.sha256}
$log=[IO.File]::ReadAllText((Join-Path $receipts 'Release.log'))
$clLines=@($log -split '[\r\n]+' | Where-Object{$_ -match '(?i)cl\.exe"?\s+/' -and $_ -match '/O2\b' -and $_ -match '/GL\b' -and $_ -match '/W4\b' -and $_ -match '/WX\b'})
$linkLines=@($log -split '[\r\n]+' | Where-Object{$_ -match '(?i)link\.exe"?\s+/' -and $_ -match '(?i)/LTCG\b'})
$features=@('USE_SDL3','USE_CODEC_MP3','USE_CODEC_VORBIS','USE_CODEC_WAVE','USE_CODEC_FLAC','USE_CODEC_OPUS','USE_VOICECHAT','USE_CODEC_XMP','USE_CODEC_UMX','USE_STEAMAUDIO')
foreach($feature in $features){if(-not @($clLines | Where-Object{$_ -match ('/D\s*'+[regex]::Escape($feature)+'\b')}).Count){throw "Full-feature compile define missing: $feature"}}
if($clLines.Count -eq 0 -or $linkLines.Count -eq 0){throw "Actual optimized compiler/linker commands missing: cl=$($clLines.Count), link=$($linkLines.Count)"}
$result=@{status='passed';revision=$entry.production_revision;archive_sha256=$entry.archive_sha256;original_source_files_rehashed=$count;unchanged_dependency_hash_checks=$depCount;compile_file_hash_checks=$compileCount;artifact_file_hash_checks=$art.files.Count;native_tool_hashes_unchanged=$tool.installed_x64_tool_facts.Count;actual_O2_GL_compiler_commands=$clLines.Count;actual_LTCG_link_commands=$linkLines.Count;runtime_executed=$false}
[IO.File]::WriteAllText((Join-Path $receipts 'independent-guest-verification.json'),(ConvertTo-Json -InputObject $result -Depth 8),[Text.UTF8Encoding]::new($false))
$transfers=@(Get-ChildItem -LiteralPath $receipts -File | ForEach-Object{@{file=$_.Name;bytes=$_.Length;sha256=(Get-FileHash -Algorithm SHA256 -LiteralPath $_.FullName).Hash.ToLowerInvariant()}})
[IO.File]::WriteAllText((Join-Path $receipts 'receipt-transfer-manifest.json'),(ConvertTo-Json -InputObject $transfers -Depth 6),[Text.UTF8Encoding]::new($false))
Write-Output ('INDEPENDENT_GUEST_PASSED source='+$count+' dependencies='+$depCount+' compile_hashes='+$compileCount+' optimized_cl='+$clLines.Count+' LTCG_link='+$linkLines.Count)
