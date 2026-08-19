param(
    [string[]]$AppKey = @(
        "system.generated.quakespasm-openvr.exe",
        "system.generated.quakespasm-openvr",
        "system.generated.quakespasm-openvr.bin"
    ),
    [string]$SteamVRBindingHost = "http://localhost:27062"
)

$ErrorActionPreference = "Stop"

function Show-MessageBox {
    param(
        [string]$Message,
        [string]$Title,
        [int]$Flags = 0x40
    )

    try {
        $shell = New-Object -ComObject WScript.Shell
        $null = $shell.Popup($Message, 0, $Title, $Flags)
    }
    catch {
        Write-Host "${Title}: $Message"
    }
}

function Write-AppKeyBindingFile {
    param(
        [string]$SourcePath,
        [string]$DestinationPath,
        [string]$BindingAppKey
    )

    $json = Get-Content -Raw -Path $SourcePath | ConvertFrom-Json
    $json.app_key = $BindingAppKey
    $jsonText = $json | ConvertTo-Json -Depth 64
    $utf8NoBom = New-Object System.Text.UTF8Encoding -ArgumentList $false
    [System.IO.File]::WriteAllText($DestinationPath, $jsonText, $utf8NoBom)
}

function Import-SteamVRBinding {
    param(
        [string]$BindingFilePath,
        [string]$ControllerType,
        [string]$BindingAppKey
    )

    $fileUri = (New-Object System.Uri -ArgumentList $BindingFilePath).AbsoluteUri
    $body = @{
        app_key         = $BindingAppKey
        controller_type = $ControllerType
        url             = $fileUri
    } | ConvertTo-Json -Compress

    Write-Host "Importing $ControllerType binding for $BindingAppKey..."

    $response = Invoke-WebRequest `
        -UseBasicParsing `
        -Uri "$SteamVRBindingHost/input/selectconfig.action" `
        -Method POST `
        -Headers @{
            "Accept"  = "application/json, text/plain, */*"
            "Origin"  = $SteamVRBindingHost
            "Referer" = "$SteamVRBindingHost/dashboard/controllerbinding.html"
        } `
        -ContentType "application/json" `
        -Body $body `
        -TimeoutSec 15

    if ($response.StatusCode -lt 200 -or $response.StatusCode -ge 300) {
        throw "SteamVR returned HTTP $($response.StatusCode)."
    }
}

$steamVRProcess = Get-Process -Name "vrcompositor" -ErrorAction SilentlyContinue
if (-not $steamVRProcess) {
    Show-MessageBox "SteamVR is not running. Start SteamVR, then run this installer again." "SteamVR Not Running" 0x30
    exit 1
}

$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Definition
$bindings = @(
    @{
        File = "quakespasm-openvr_legacy_knuckles.json"
        Type = "knuckles"
    }
)

$steamVRImportDir = Join-Path -Path ([Environment]::GetFolderPath("MyDocuments")) -ChildPath "steamvr\input\imports"
if (-not (Test-Path $steamVRImportDir)) {
    $null = New-Item -ItemType Directory -Path $steamVRImportDir -Force
}

$successes = New-Object System.Collections.Generic.List[string]
$failures = New-Object System.Collections.Generic.List[string]

foreach ($key in $AppKey) {
    foreach ($binding in $bindings) {
        $sourcePath = Join-Path -Path $scriptDir -ChildPath $binding.File
        if (-not (Test-Path $sourcePath)) {
            $failures.Add("$key ($($binding.Type)): binding file not found: $sourcePath")
            continue
        }

        $destName = "$($key)_$($binding.Type).json"
        $destPath = Join-Path -Path $steamVRImportDir -ChildPath $destName

        try {
            Write-AppKeyBindingFile -SourcePath $sourcePath -DestinationPath $destPath -BindingAppKey $key
            Import-SteamVRBinding -BindingFilePath $destPath -ControllerType $binding.Type -BindingAppKey $key
            $successes.Add("$key ($($binding.Type))")
        }
        catch {
            $failures.Add("$key ($($binding.Type)): $_")
        }
    }
}

if ($successes.Count -gt 0) {
    Write-Host ""
    Write-Host "Installed SteamVR bindings:"
    foreach ($item in $successes) {
        Write-Host "  $item"
    }

    if ($failures.Count -gt 0) {
        Write-Host ""
        Write-Host "Some optional app-key candidates failed:"
        foreach ($item in $failures) {
            Write-Host "  $item"
        }
    }

    Show-MessageBox "Recommended Quakespasm VR SteamVR bindings were installed. If the game is already running, restart it before testing the controllers." "Bindings Installed" 0x40
    exit 0
}

Write-Host ""
Write-Host "Failed to install any bindings:"
foreach ($item in $failures) {
    Write-Host "  $item"
}

Show-MessageBox "SteamVR did not accept the Quakespasm VR bindings. Launch quakespasm-openvr.exe once in SteamVR, then run this installer again." "Binding Install Failed" 0x30
exit 1
