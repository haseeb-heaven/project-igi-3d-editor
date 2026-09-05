<#
.SYNOPSIS
    Single-line modular runner for Project IGI smart camera verification.

.DESCRIPTION
    Runs modular 360-degree screenshot and model texture verification across levels and categories.
    Supports single levels or all 14 levels, object categories (Buildings, AI, RigidObjects, Vehicles, All),
    and bounding object counts (e.g. 1, 3, or all).

.EXAMPLE
    .\Run-SmartTest.ps1 -Level 1 -Category Buildings -MaxObjects 3
    .\Run-SmartTest.ps1 -Level 1 -AllObjects
    .\Run-SmartTest.ps1 -AllLevels -Category AI -MaxObjects 1
    .\Run-SmartTest.ps1 -Level 1 -Category RigidObjects -Resume
#>
[CmdletBinding()]
param(
    [ValidateRange(1, 14)][int[]]$Level = @(1),
    [switch]$AllLevels,
    [ValidateSet('All', 'Buildings', 'RigidObjects', 'Vehicles', 'AI')][string]$Category = 'All',
    [string[]]$ObjectTypes = @(),
    [int]$MaxObjects = 3,
    [switch]$AllObjects,
    [switch]$DistinctTypes,
    [ValidateRange(1, 10)][int]$ViewCount = 10,
    [switch]$PrepareOnly,
    [switch]$Resume,
    [switch]$LegacySerial,
    [string]$EditorExePath = '',
    [string]$ArtifactsRoot = ''
)

$ErrorActionPreference = 'Stop'
$repoRoot = $PSScriptRoot

if ([string]::IsNullOrWhiteSpace($EditorExePath)) {
    $binCandidate = Join-Path $repoRoot 'bin\Release\igi1ed.exe'
    if (Test-Path -LiteralPath $binCandidate) {
        $EditorExePath = $binCandidate
    }
}

# Clean up any lingering editor instances before starting
$old = @(Get-Process igi1ed -ErrorAction SilentlyContinue)
if ($old.Count -gt 0) {
    Write-Host "[Info] Closing $($old.Count) lingering igi1ed process(es)..." -ForegroundColor Yellow
    foreach ($p in $old) {
        & taskkill.exe /PID $p.Id 2>$null | Out-Null
        if (-not $p.WaitForExit(2500)) {
            Stop-Process -Id $p.Id -Force -ErrorAction SilentlyContinue
        }
    }
    Start-Sleep -Milliseconds 500
}

# Resolve parameters
if ($AllObjects) {
    $MaxObjects = 0
}

$levelStr = if ($AllLevels) { 'all' } else { ($Level -join '-') }
if ([string]::IsNullOrWhiteSpace($ArtifactsRoot)) {
    $stamp = (Get-Date).ToString('yyyyMMdd-HHmmss')
    $ArtifactsRoot = Join-Path $repoRoot "artifacts\e2e\run-lvl$($levelStr)-$($Category.ToLower())-$stamp"
}

Write-Host "========================================================" -ForegroundColor Cyan
Write-Host "  Project IGI Editor Smart Verification Runner" -ForegroundColor Cyan
Write-Host "========================================================" -ForegroundColor Cyan
Write-Host "Levels     : $(if ($AllLevels) { '1 to 14' } else { $Level -join ', ' })"
Write-Host "Category   : $Category"
Write-Host "MaxObjects : $(if ($MaxObjects -eq 0) { 'ALL objects' } else { $MaxObjects })"
Write-Host "Artifacts  : $ArtifactsRoot"
Write-Host "--------------------------------------------------------"

$categoryTypes = @{
    All = @()
    Buildings = @('Building','Door','Terminal','Switch','AlarmControl')
    RigidObjects = @('EditRigidObj','Static','Dynamic','SplineObj','SplineObjWaypoint','ExplodeObject')
    Vehicles = @('Car','Heli','Train')
    AI = @('HumanAI','HumanSoldier','AISquad','AIGraph','PatrolPath','PatrolPathCommand')
}

if ($LegacySerial) {
    $toolPath = Join-Path $repoRoot 'tools\e2e\Invoke-SmartVerificationMatrix.ps1'
    $runnerArgs = @('-ArtifactsRoot', $ArtifactsRoot, '-Categories', $Category, '-AllowConfigMutation', '-RetryCount', 1)
    if ($ObjectTypes.Count) { $runnerArgs += @('-ObjectTypes', ($ObjectTypes -join ',')) }
    if ($AllLevels) { $runnerArgs += '-AllLevels' } else { $runnerArgs += @('-Levels', ($Level -join ',')) }
    if ($MaxObjects -gt 0) { $runnerArgs += @('-MaxObjects', $MaxObjects) } else { $runnerArgs += '-AllObjects' }
    if ($PrepareOnly) { $runnerArgs += '-PrepareOnly' }
    if ($Resume) { $runnerArgs += '-Resume' }
    if ($DistinctTypes) { $runnerArgs += '-DistinctTypes' }
    if ($ViewCount -ne 10) { $runnerArgs += @('-ViewCount', $ViewCount) }
    & powershell.exe -NoProfile -ExecutionPolicy Bypass -File $toolPath @runnerArgs
    $exitCode = $LASTEXITCODE
} else {
    if ($Resume) { throw '-Resume is only supported by -LegacySerial; single-session runs use a fresh artifact directory.' }
    $toolPath = Join-Path $repoRoot 'tools\e2e\Invoke-SmartNativeCaptureSession.ps1'
    $levels = if ($AllLevels) { @(1..14) } else { @($Level | Sort-Object -Unique) }
    $results = @()
    $exitCode = 0
    foreach ($levelNumber in $levels) {
        $levelRoot = Join-Path $ArtifactsRoot ('level' + $levelNumber)
        $runnerArgs = @('-ArtifactsRoot', $levelRoot, '-GameRoot', 'D:\IGI1', '-Level', $levelNumber, '-ViewCount', $ViewCount)
        if (-not [string]::IsNullOrWhiteSpace($EditorExePath)) { $runnerArgs += @('-EditorExePath', $EditorExePath) }
        if ($MaxObjects -gt 0) { $runnerArgs += @('-MaxObjects', $MaxObjects) }
        $selectedTypes = @()
        if ($Category -ne 'All') { $selectedTypes += $categoryTypes[$Category] }
        if ($ObjectTypes.Count) { $selectedTypes += $ObjectTypes }
        $selectedTypes = @($selectedTypes | Select-Object -Unique)
        if ($selectedTypes.Count) { $runnerArgs += @('-IncludeTypes', ($selectedTypes -join ',')) }
        if ($DistinctTypes) { $runnerArgs += '-DistinctTypes' }
        if ($PrepareOnly) { $runnerArgs += '-PrepareOnly' }
        & powershell.exe -NoProfile -ExecutionPolicy Bypass -File $toolPath @runnerArgs
        $levelExit = $LASTEXITCODE
        $levelStatePath = Join-Path $levelRoot 'batch.json'
        if (Test-Path -LiteralPath $levelStatePath) {
            $results += (Get-Content -LiteralPath $levelStatePath -Raw | ConvertFrom-Json)
        } else {
            $results += [pscustomobject]@{level=$levelNumber;status='FAIL';failure='No batch.json was produced.'}
        }
        if ($levelExit -ne 0) { $exitCode = $levelExit; break }
    }
    $results | ConvertTo-Json -Depth 15 | Set-Content -LiteralPath (Join-Path $ArtifactsRoot 'summary.json') -Encoding UTF8
    Write-Host "[Results Summary]" -ForegroundColor Cyan
    foreach ($res in $results) {
        $color = if ($res.status -eq 'PASS' -or $res.status -eq 'PREPARED') { 'Green' } else { 'Red' }
        Write-Host ("  Level {0}: Status={1}, Objects={2}, Launches={3}, Closes={4}" -f $res.level,$res.status,$res.selectableObjects,$res.launchCount,$res.closeCount) -ForegroundColor $color
    }
}

Write-Host "--------------------------------------------------------"

if ($exitCode -eq 0) {
    Write-Host "`n[SUCCESS] Smart verification completed successfully!" -ForegroundColor Green
    Write-Host "Screenshots and logs saved in: $ArtifactsRoot`n"
} else {
    Write-Host "`n[FAIL] Verification finished with errors (Exit Code $exitCode)." -ForegroundColor Red
    Write-Host "Inspect batch details in: $ArtifactsRoot`n"
}

exit $exitCode
