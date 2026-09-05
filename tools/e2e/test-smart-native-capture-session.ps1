$ErrorActionPreference = 'Stop'
$scriptPath = Join-Path $PSScriptRoot 'Invoke-SmartNativeCaptureSession.ps1'
$inventoryPath = Join-Path $PSScriptRoot '../../artifacts/task6-metadata-manifest.json'
$tokens = $null; $errors = $null
[Management.Automation.Language.Parser]::ParseFile($scriptPath, [ref]$tokens, [ref]$errors) | Out-Null
if ($errors.Count) { throw "Native capture runner has PowerShell parse errors: $($errors[0].Message)" }
$runnerText = Get-Content -LiteralPath $scriptPath -Raw
if ($runnerText -notmatch '--developer-mode') { throw 'Native capture must start the editor in developer mode so its command watcher is available.' }
if ($runnerText -notmatch 'Selected editor does not consume developer commands') { throw 'Native capture must fail clearly when the selected editor lacks its command watcher.' }
if ($runnerText -notmatch '__smart_capture_probe_missing__') { throw 'Native capture must probe that the developer command watcher consumes a harmless missing-model command.' }
if ($runnerText -notmatch 'Wait-ForCaptureFiles \$allPaths') { throw 'Native capture must wait for every emitted view before restoring shared screenshot paths.' }

$inventory = Get-Content -LiteralPath $inventoryPath -Raw | ConvertFrom-Json
$editorExePath = (Resolve-Path (Join-Path $PSScriptRoot '../../bin/Release/igi1ed.exe')).Path
$fixture = $null
foreach ($level in @($inventory.levels)) {
    $candidate = @($level.inventory | Where-Object {
        $_.modelId -and @($_.authoredPosition).Count -eq 3 -and @($_.authoredRotation).Count -eq 3
    } | Select-Object -First 1)
    if ($candidate.Count -eq 1) {
        $fixture = [pscustomobject]@{level=[int]$level.level;type=[string]$candidate[0].type}
        break
    }
}
if ($null -eq $fixture) { throw 'Inventory has no renderable fixture for the native-session contract test.' }

$root = Join-Path ([IO.Path]::GetTempPath()) ('igi-native-session-' + [Guid]::NewGuid().ToString('N'))
try {
    & powershell.exe -NoProfile -ExecutionPolicy Bypass -File $scriptPath -ArtifactsRoot $root -InventoryPath $inventoryPath -GameRoot 'D:\IGI1' -EditorExePath $editorExePath -Level $fixture.level -IncludeTypes $fixture.type -MaxObjects 1 -ViewCount 3 -PrepareOnly
    if ($LASTEXITCODE -ne 0) { throw "Prepare-only runner returned $LASTEXITCODE." }
    $state = Get-Content (Join-Path $root 'batch.json') -Raw | ConvertFrom-Json
    if ($state.status -ne 'PREPARED') { throw "Expected PREPARED state, got $($state.status)." }
    if ([string]$state.editorExecutable -ne $editorExePath) { throw 'Native session did not record the explicitly selected editor executable.' }
    if ([string]$state.logPath -ne (Join-Path (Split-Path -Parent $editorExePath) 'igi1ed.log')) { throw 'Native session did not select the log beside the explicit editor executable.' }
    if ([int]$state.selectableObjects -ne 1 -or [int]$state.launchCount -ne 1 -or [int]$state.closeCount -ne 1) { throw 'Native single-session contract is incorrect.' }
    if ([int]$state.viewCount -ne 3 -or [int]$state.screenshotsExpected -ne 3) { throw 'Native capture view contract is incorrect.' }
    'PASS: native-session plan has one editor lifecycle and three requested views.'
} finally {
    if (Test-Path -LiteralPath $root) { Remove-Item -LiteralPath $root -Recurse -Force -ErrorAction SilentlyContinue }
}
