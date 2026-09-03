$ErrorActionPreference = 'Stop'

function Fail([string]$Message) { throw [System.InvalidOperationException]::new($Message) }
function Require([bool]$Condition, [string]$Message) {
    if (-not $Condition) { Fail $Message }
}
function Values($Value) {
    if ($null -eq $Value) { return @() }
    return @($Value)
}
function Get-IndependentTaskIds([string]$Source) {
    $taskIds = New-Object System.Collections.Generic.List[string]
    $anonymous = 0
    foreach ($match in [regex]::Matches($Source, '\bTask_New\s*\(\s*(-?[0-9]+|"(?:\\.|[^"])*")')) {
        $rawTaskId = $match.Groups[1].Value
        if ($rawTaskId.StartsWith('"') -and $rawTaskId.EndsWith('"')) {
            $rawTaskId = $rawTaskId.Substring(1, $rawTaskId.Length - 2).Replace('\"', '"').Replace('\\', '\')
        }
        if ($rawTaskId -eq '-1') {
            $anonymous++
            $taskIds.Add("-1#$anonymous")
        } else {
            $taskIds.Add($rawTaskId)
        }
    }
    return $taskIds.ToArray()
}

$gameRoot = 'D:\IGI1'
$cataloguePath = Join-Path $PSScriptRoot 'editor-workflow-catalogue.json'
$generatorPath = Join-Path $PSScriptRoot 'New-EditorWorkflowManifest.ps1'
$converterPath = Join-Path (Split-Path -Parent (Split-Path -Parent $PSScriptRoot)) 'assets\editor\tools\igi1conv\igi1conv.exe'
$manifestPath = Join-Path $env:TEMP ('igi-editor-workflow-manifest-' + [guid]::NewGuid().ToString('N') + '.json')
$decompileRoot = Join-Path $env:TEMP ('igi-editor-workflow-contract-' + [guid]::NewGuid().ToString('N'))
$locationPushed = $false

try {
    if (-not (Test-Path -LiteralPath $cataloguePath -PathType Leaf)) {
        Fail "Editor workflow catalogue is missing: $cataloguePath"
    }
    if (-not (Test-Path -LiteralPath $generatorPath -PathType Leaf)) {
        Fail "Editor workflow manifest generator is missing: $generatorPath"
    }
    if (-not (Test-Path -LiteralPath $converterPath -PathType Leaf)) {
        Fail "Installed converter is missing: $converterPath"
    }
    if (-not (Test-Path -LiteralPath $gameRoot -PathType Container)) {
        Fail "Game root is missing: $gameRoot"
    }

    Push-Location -LiteralPath $gameRoot
    $locationPushed = $true
    $childWorkingDirectory = [string](& pwsh -NoProfile -Command '(Get-Location).ProviderPath')
    Require ($childWorkingDirectory.TrimEnd('\') -ceq $gameRoot.TrimEnd('\')) "Generator child working directory must be $gameRoot, got $childWorkingDirectory."
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
    Require ([string]$manifest.inventoryHash -cmatch '^[a-f0-9]{64}$') 'Manifest must contain a lowercase 64-character SHA-256 inventory hash.'

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
        $sourcePath = [string]$level.sourcePath
        Require ($sourcePath -notmatch '\\\\') "Level $($level.level) source path contains doubled separators: $sourcePath"
        $objectsQvm = Join-Path $gameRoot $sourcePath
        Require (Test-Path -LiteralPath $objectsQvm -PathType Leaf) "Level $($level.level) source path is missing: $objectsQvm"
        $expectedSourceHash = (Get-FileHash -LiteralPath $objectsQvm -Algorithm SHA256).Hash.ToLowerInvariant()
        Require ([string]$level.sourceHash -cmatch '^[a-f0-9]{64}$') "Level $($level.level) sourceHash is not a lowercase 64-character SHA-256 value."
        Require ([string]$level.sourceHash -ceq $expectedSourceHash) "Level $($level.level) sourceHash does not match $objectsQvm."

        if (-not (Test-Path -LiteralPath $decompileRoot -PathType Container)) { New-Item -ItemType Directory -Path $decompileRoot -Force | Out-Null }
        $decompiled = Join-Path $decompileRoot ("level$($level.level)-objects.qsc")
        & $converterPath qvm decompile $objectsQvm -o $decompiled
        Require ($LASTEXITCODE -eq 0) "Independent objects.qvm decompile failed for level $($level.level) with exit code $LASTEXITCODE."
        Require (Test-Path -LiteralPath $decompiled -PathType Leaf) "Independent decompile did not write $decompiled."
        $independentTaskIds = @(Get-IndependentTaskIds (Get-Content -LiteralPath $decompiled -Raw))
        Require ($independentTaskIds.Count -gt 0) "Independent decompile found no Task_New instances for level $($level.level)."

        $taskIds = @(Values $level.taskIds | ForEach-Object { [string]$_ })
        $inventory = Values $level.inventory
        $inventoryTaskIds = @($inventory | ForEach-Object { [string]$_.taskId })
        Require (($inventoryTaskIds | Sort-Object -Unique).Count -eq $inventoryTaskIds.Count) "Level $($level.level) inventory contains duplicate task IDs."
        Require (($taskIds | Sort-Object -Unique).Count -eq $taskIds.Count) "Level $($level.level) declared task IDs contain duplicates."
        Require (($independentTaskIds | Sort-Object -Unique).Count -eq $independentTaskIds.Count) "Independent decompile found duplicate task IDs for level $($level.level)."
        Require ((Compare-Object -ReferenceObject ($independentTaskIds | Sort-Object) -DifferenceObject ($taskIds | Sort-Object)).Count -eq 0) "Level $($level.level) declared task IDs do not match independently decompiled Task_New instances."
        Require ((Compare-Object -ReferenceObject ($independentTaskIds | Sort-Object) -DifferenceObject ($inventoryTaskIds | Sort-Object)).Count -eq 0) "Level $($level.level) inventory task IDs are not exhaustive against independently decompiled Task_New instances."
        foreach ($entry in $inventory) {
            foreach ($field in @('level', 'taskId', 'type', 'modelId', 'authoredPosition', 'authoredRotation', 'textures', 'lods', 'sounds', 'sourceHash')) {
                Require ($null -ne $entry.$field) "Level $($level.level) task $($entry.taskId) is missing $field."
            }
            Require ([string]$entry.sourceHash -cmatch '^[a-f0-9]{64}$') "Level $($level.level) task $($entry.taskId) sourceHash is not a lowercase 64-character SHA-256 value."
            Require ([string]$entry.sourceHash -ceq $expectedSourceHash) "Level $($level.level) task $($entry.taskId) sourceHash does not match $objectsQvm."
        }
        foreach ($graphFile in Values $level.discovery.graphs) {
            $graphScenarios = @($scenarios | Where-Object { $_.level -eq $level.level -and $_.action -eq 'graph-overlay' -and $_.anchor.graphFile -eq $graphFile })
            Require ($graphScenarios.Count -eq 1) "Level $($level.level) graph file '$graphFile' must have exactly one graph-overlay scenario."
        }
    }

    Write-Host 'Editor workflow manifest contract: PASS'
} finally {
    if ($locationPushed) { Pop-Location }
    Remove-Item -LiteralPath $manifestPath -Force -ErrorAction SilentlyContinue
    Remove-Item -LiteralPath $decompileRoot -Recurse -Force -ErrorAction SilentlyContinue
}
