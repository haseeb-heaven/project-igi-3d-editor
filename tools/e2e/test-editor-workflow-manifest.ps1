$ErrorActionPreference = 'Stop'

function Fail([string]$Message) { throw [System.InvalidOperationException]::new($Message) }
function Require([bool]$Condition, [string]$Message) {
    if (-not $Condition) { Fail $Message }
}
function Values($Value) {
    if ($null -eq $Value) { return @() }
    return @($Value)
}

$gameRoot = 'D:\IGI1'
$cataloguePath = Join-Path $PSScriptRoot 'editor-workflow-catalogue.json'
$generatorPath = Join-Path $PSScriptRoot 'New-EditorWorkflowManifest.ps1'
$manifestPath = Join-Path $env:TEMP ('igi-editor-workflow-manifest-' + [guid]::NewGuid().ToString('N') + '.json')

try {
    if (-not (Test-Path -LiteralPath $cataloguePath -PathType Leaf)) {
        Fail "Editor workflow catalogue is missing: $cataloguePath"
    }
    if (-not (Test-Path -LiteralPath $generatorPath -PathType Leaf)) {
        Fail "Editor workflow manifest generator is missing: $generatorPath"
    }

    & pwsh -NoProfile -ExecutionPolicy Bypass -File $generatorPath -GameRoot $gameRoot -OutputPath $manifestPath
    Require ($LASTEXITCODE -eq 0) "Editor workflow manifest generator failed with exit code $LASTEXITCODE."
    Require (Test-Path -LiteralPath $manifestPath -PathType Leaf) "Generator did not write $manifestPath."

    $catalogue = Get-Content -LiteralPath $cataloguePath -Raw | ConvertFrom-Json
    $manifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
    $catalogueEntries = Values $catalogue.actions
    Require ($catalogueEntries.Count -gt 0) 'Catalogue must declare at least one action.'
    foreach ($entry in $catalogueEntries) {
        foreach ($field in @('name', 'action', 'applicability', 'requiredSteps', 'uiOracle', 'stateOracle')) {
            Require ($null -ne $entry.$field -and -not [string]::IsNullOrWhiteSpace([string]$entry.$field)) "Catalogue action '$($entry.name)' is missing $field."
        }
    }

    $levels = Values $manifest.levels
    Require ($levels.Count -eq 14) "Expected 14 levels, got $($levels.Count)."
    $numbers = @($levels | ForEach-Object { [int]$_.level } | Sort-Object)
    Require (($numbers -join ',') -eq '1,2,3,4,5,6,7,8,9,10,11,12,13,14') "Manifest levels were '$($numbers -join ',')'."
    Require (-not [string]::IsNullOrWhiteSpace([string]$manifest.inventoryHash)) 'Manifest must contain an inventory hash.'

    $scenarios = Values $manifest.scenarios
    $exclusions = Values $manifest.exclusions
    foreach ($action in $catalogueEntries) {
        $applicable = @($scenarios | Where-Object { $_.action -eq $action.name })
        $excluded = @($exclusions | Where-Object { $_.action -eq $action.name -and -not [string]::IsNullOrWhiteSpace([string]$_.reason) })
        Require (($applicable.Count + $excluded.Count) -gt 0) "Catalogue action '$($action.name)' has no applicable scenario or explicit exclusion."
    }

    $anchors = @{}
    foreach ($scenario in $scenarios) {
        foreach ($field in @('level', 'taskId', 'workflow')) {
            Require ($null -ne $scenario.$field -and -not [string]::IsNullOrWhiteSpace([string]$scenario.$field)) "Scenario is missing $field."
        }
        $key = "$($scenario.level)|$($scenario.taskId)|$($scenario.workflow)"
        Require (-not $anchors.ContainsKey($key)) "Duplicate scenario anchor: $key"
        $anchors[$key] = $true
    }

    foreach ($level in $levels) {
        foreach ($action in $catalogueEntries) {
            $applicable = @($scenarios | Where-Object { $_.level -eq $level.level -and $_.action -eq $action.name })
            $excluded = @($exclusions | Where-Object { $_.level -eq $level.level -and $_.action -eq $action.name })
            Require (($applicable.Count + $excluded.Count) -gt 0) "Level $($level.level) action '$($action.name)' has no scenario or explicit exclusion."
            Require ($excluded.Count -le 1) "Level $($level.level) action '$($action.name)' has duplicate exclusions."
            foreach ($exclusion in $excluded) {
                Require (-not [string]::IsNullOrWhiteSpace([string]$exclusion.reason)) "Level $($level.level) action '$($action.name)' has an exclusion without a reason."
            }
        }
        $taskIds = Values $level.taskIds | ForEach-Object { [string]$_ }
        $inventory = Values $level.inventory
        $inventoryTaskIds = @($inventory | ForEach-Object { [string]$_.taskId })
        Require (($inventoryTaskIds | Sort-Object -Unique).Count -eq $inventoryTaskIds.Count) "Level $($level.level) inventory contains duplicate task IDs."
        Require (($taskIds | Sort-Object -Unique).Count -eq $taskIds.Count) "Level $($level.level) declared task IDs contain duplicates."
        Require ((Compare-Object -ReferenceObject ($taskIds | Sort-Object) -DifferenceObject ($inventoryTaskIds | Sort-Object)).Count -eq 0) "Level $($level.level) inventory task IDs are not exhaustive."
        foreach ($entry in $inventory) {
            foreach ($field in @('level', 'taskId', 'type', 'modelId', 'authoredPosition', 'authoredRotation', 'textures', 'lods', 'sounds', 'sourceHash')) {
                Require ($null -ne $entry.$field) "Level $($level.level) task $($entry.taskId) is missing $field."
            }
        }
    }

    Write-Host 'Editor workflow manifest contract: PASS'
} finally {
    Remove-Item -LiteralPath $manifestPath -Force -ErrorAction SilentlyContinue
}
