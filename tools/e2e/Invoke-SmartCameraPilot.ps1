[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$CameraPlan,
    [Parameter(Mandatory)][string]$ArtifactsRoot,
    [string]$GameRoot = 'D:\IGI1',
    [ValidateRange(1,14)][int]$Level = 1,
    [ValidatePattern('^\d+$')][string]$TaskId = '1105',
    [ValidateRange(1,10)][int]$ViewCount = 1,
    [switch]$AllowConfigMutation
)
$ErrorActionPreference = 'Stop'
if (-not $AllowConfigMutation) { throw 'Requires -AllowConfigMutation; QED config is temporarily changed and restored.' }
if (Get-Process igi1ed -ErrorAction SilentlyContinue) { throw 'Close the existing editor before the serial pilot.' }
$plan = Get-Content -LiteralPath $CameraPlan -Raw | ConvertFrom-Json
if (@($plan.views).Count -ne 10) { throw 'Expected ten planned camera views.' }
foreach ($view in $plan.views) {
    if ($view.name -notmatch '^(azimuth-(0|45|90|135|180|225|270|315)|above|below)$' -or @($view.position).Count -ne 3) { throw 'Invalid camera view.' }
    foreach ($number in @($view.position)+@($view.yaw,$view.pitch,$view.roll)) {
        $value = [double]$number
        if ([double]::IsNaN($value) -or [double]::IsInfinity($value)) { throw 'Non-finite camera pose.' }
    }
}
if (@($plan.views.name | Select-Object -Unique).Count -ne 10) { throw 'Duplicate camera views.' }
$ArtifactsRoot = [IO.Path]::GetFullPath($ArtifactsRoot)
if (Test-Path $ArtifactsRoot) { throw 'Use a fresh artifact directory; recovery backups must not be overwritten.' }
New-Item -ItemType Directory $ArtifactsRoot | Out-Null
$backup = Join-Path $ArtifactsRoot 'qed-backup'
New-Item -ItemType Directory $backup | Out-Null
$qed = Join-Path $GameRoot 'editor/qed'
$files = @(Get-ChildItem $qed -File | Where-Object Extension -in @('.qsc','.qvm'))
foreach ($file in @($files | Where-Object Extension -eq '.qsc')) {
    if (-not (Test-Path ([IO.Path]::ChangeExtension($file.FullName,'.qvm')))) { throw 'Compile missing QED siblings before capture so restoration covers every generated file.' }
}
$snapshots = @(foreach ($file in $files) {
    Copy-Item -LiteralPath $file.FullName -Destination $backup
    [pscustomobject]@{name=$file.Name;sha256=(Get-FileHash $file.FullName -Algorithm SHA256).Hash}
})
$snapshots | ConvertTo-Json | Set-Content (Join-Path $backup 'hashes.json') -Encoding UTF8
$config = Join-Path $qed 'qedconfig.qsc'
$original = Get-Content -LiteralPath $config -Raw
$invariant = [Globalization.CultureInfo]::InvariantCulture
try {
    foreach ($view in @($plan.views | Select-Object -First $ViewCount)) {
        $text = $original
        $position = @($view.position | ForEach-Object { ([double]$_).ToString('R',$invariant) }) -join ', '
        # An all-zero orientation means "use spawn orientation" in the editor.
        # 360 degrees is the same heading as zero but explicitly selects the pose.
        $yaw = if ($view.yaw -eq 0 -and $view.pitch -eq 0 -and $view.roll -eq 0) { 360 } else { $view.yaw }
        $orientation = @($yaw,$view.pitch,$view.roll | ForEach-Object { ([double]$_).ToString('R',$invariant) }) -join ', '
        $settings = @{QEDSetCameraPosition=$position;QEDSetCameraOrientation=$orientation;QEDAutoSaveEnabled='FALSE';QEDSaveConfigOnExit='FALSE'}
        foreach ($name in $settings.Keys) {
            $pattern = '(?m)^\s*'+[regex]::Escape($name)+'\([^\r\n;]*\);'
            if ([regex]::Matches($text,$pattern).Count -ne 1) { throw "Expected exactly one $name config declaration." }
            $text = [regex]::Replace($text,$pattern,($name+'('+$settings[$name]+');'))
        }
        [IO.File]::WriteAllText($config,$text,[Text.UTF8Encoding]::new($false))
        $steps = @(
            @{id='launch';type='launch_editor';level=$Level},
            @{id='loaded';type='wait_for_log';pattern="\[App\] LoadLevel\(\) COMPLETE for level $Level";timeoutSeconds=120},
            @{id='health';type='assert_process'},
            @{id='find';type='key';key='CTRL+SHIFT+I'},
            @{id='id';type='type_text';text=$TaskId},
            @{id='select';type='key';key='ENTER'},
            @{id='settle';type='wait';seconds=2},
            @{id='capture';type='screenshot';name=$view.name},
            @{id='window';type='capture_window_state'},
            @{id='close';type='close_editor';force=$true}
        )
        $manifest = Join-Path $ArtifactsRoot ($view.name+'.json')
        @{scenarios=@(@{name=$view.name;level=$Level;requiresMutation=$false;steps=$steps})} | ConvertTo-Json -Depth 10 | Set-Content $manifest -Encoding UTF8
        & powershell.exe -NoProfile -ExecutionPolicy Bypass -File (Join-Path $PSScriptRoot 'editor-e2e.ps1') -GameRoot $GameRoot -ScenarioPath $manifest -ArtifactsRoot (Join-Path $ArtifactsRoot $view.name)
        if ($LASTEXITCODE -ne 0) { throw "Capture failed for $($view.name)." }
        # Retain a stable log snapshot before the next launch appends to it.
        Copy-Item (Join-Path $GameRoot 'igi1ed.log') (Join-Path $ArtifactsRoot ($view.name+'.log'))
    }
} finally {
    $remaining = @(Get-Process igi1ed -ErrorAction SilentlyContinue)
    foreach ($process in $remaining) { if (-not $process.WaitForExit(5000)) { throw "Editor still running; retained recovery backup at $backup. Close it before restoring." } }
    foreach ($snapshot in $snapshots) {
        $dest = Join-Path $qed $snapshot.name
        Copy-Item (Join-Path $backup $snapshot.name) $dest -Force
        if ((Get-FileHash $dest -Algorithm SHA256).Hash -ne $snapshot.sha256) { throw "Restore mismatch: $dest; backup retained at $backup" }
    }
    'Original QED files restored and hashes verified.'
}
