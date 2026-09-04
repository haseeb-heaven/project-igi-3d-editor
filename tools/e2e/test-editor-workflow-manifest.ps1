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

function New-RunnerFixtureFile([string]$Path, $Manifest) {
    $Manifest | ConvertTo-Json -Depth 15 | Set-Content -LiteralPath $Path -Encoding UTF8
}
function Invoke-RunnerValidate([string]$ManifestPath) {
    # Run the child out of process so an intentional validation rejection cannot
    # be promoted by this script's $ErrorActionPreference; the exit code and
    # rendered message stay authoritative.
    $lines = @(& pwsh -NoProfile -ExecutionPolicy Bypass -File $runner -ValidateOnly -ScenarioPath $ManifestPath 2>&1)
    return [pscustomobject]@{ ExitCode = [int]$LASTEXITCODE; Output = ($lines | Out-String) }
}
function Assert-OutputContains([string]$Text, [string]$Keyword, [string]$CaseLabel) {
    Require ($Text.IndexOf($Keyword, [System.StringComparison]::OrdinalIgnoreCase) -ge 0) "$CaseLabel output did not mention '$Keyword': $Text"
}

$gameRoot = 'D:\IGI1'
$runner = Join-Path $PSScriptRoot 'editor-e2e.ps1'
$cataloguePath = Join-Path $PSScriptRoot 'editor-workflow-catalogue.json'
$generatorPath = Join-Path $PSScriptRoot 'New-EditorWorkflowManifest.ps1'
$converterPath = Join-Path (Split-Path -Parent (Split-Path -Parent $PSScriptRoot)) 'assets\editor\tools\igi1conv\igi1conv.exe'
$manifestPath = Join-Path $env:TEMP ('igi-editor-workflow-manifest-' + [guid]::NewGuid().ToString('N') + '.json')
$decompileRoot = Join-Path $env:TEMP ('igi-editor-workflow-contract-' + [guid]::NewGuid().ToString('N'))
$runnerFixtureRoot = Join-Path $env:TEMP ('igi-editor-runner-contract-' + [guid]::NewGuid().ToString('N'))
$locationPushed = $false

try {
    if (-not (Test-Path -LiteralPath $cataloguePath -PathType Leaf)) {
        Fail "Editor workflow catalogue is missing: $cataloguePath"
    }
    if (-not (Test-Path -LiteralPath $generatorPath -PathType Leaf)) {
        Fail "Editor workflow manifest generator is missing: $generatorPath"
    }
    if (-not (Test-Path -LiteralPath $runner -PathType Leaf)) {
        Fail "Editor E2E runner is missing: $runner"
    }
    if (-not (Test-Path -LiteralPath $converterPath -PathType Leaf)) {
        Fail "Installed converter is missing: $converterPath"
    }
    if (-not (Test-Path -LiteralPath $gameRoot -PathType Container)) {
        Fail "Game root is missing: $gameRoot"
    }
    New-Item -ItemType Directory -Path $runnerFixtureRoot -Force | Out-Null

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
            foreach ($field in @('level', 'taskId', 'type', 'modelId', 'authoredPosition', 'authoredRotation', 'textures', 'lods', 'sounds', 'sourceHash', 'renderable', 'hasModel', 'hasPosition', 'hasRotation')) {
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

    # ---- Editor control/persistence coverage contract (Task 3) ----
    # Controls that exist on the editor for every level must have a generated
    # scenario on every one of the 14 levels, never a silent exclusion.  The
    # conditional controls (lightmap mode, terrain fog, level change) are
    # allowed an explicit corpus exclusion only where the corpus lacks the
    # feature.
    $alwaysApplicableControls = @(
        'editor-pause-resume', 'editor-task-tree', 'editor-terrain-shortcut',
        'editor-font-size', 'editor-autosave', 'editor-logging',
        'editor-music', 'editor-collision-clip', 'editor-save',
        'editor-reset', 'editor-graceful-quit', 'editor-cursor-state'
    )
    foreach ($control in $alwaysApplicableControls) {
        foreach ($level in $levels) {
            $controlScenarios = @($scenarios | Where-Object { $_.level -eq $level.level -and $_.action -eq $control })
            Require ($controlScenarios.Count -eq 1) "Level $($level.level) control '$control' must have exactly one generated scenario; found $($controlScenarios.Count)."
        }
    }
    $conditionalControls = @('editor-lightmap-mode', 'editor-terrain-fog', 'editor-level-change')
    foreach ($control in $conditionalControls) {
        $total = @($scenarios | Where-Object { $_.action -eq $control }).Count
        $excluded = @($exclusions | Where-Object { $_.action -eq $control }).Count
        Require (($total + $excluded) -gt 0) "Conditional control '$control' has no scenario or exclusion anywhere."
    }
    $controlAnchors = @($scenarios | Where-Object { $_.taskId -eq 'editor-control' })
    foreach ($scenario in $controlAnchors) {
        foreach ($field in @('action', 'workflow', 'level', 'taskId', 'type', 'anchor')) {
            Require ($null -ne $scenario.$field) "Control scenario on level $($scenario.level) is missing $field."
        }
        Require ($scenario.workflow -eq $scenario.action) "Control scenario '$($scenario.action)' workflow must equal its action."
        Require ($scenario.type -eq 'EditorControl') "Control scenario '$($scenario.action)' type must be EditorControl."
    }

    # ---- Object visual orbit coverage contract (Task 4) ----
    # Every renderable instance must be internally consistent (renderable iff
    # it has an authored model and a 3-component position; orientation is
    # recorded but not a gate because soldiers/cameras carry yaw-only Angle
    # fields) and must carry exactly one object-visual-orbit scenario whose
    # anchor records model/position/rotation/LODs and the ten deterministic
    # views.  The orbit-required inventory fields stay stable so a task can
    # never silently lose its visual anchor.
    $orbitAnchorsByKey = @{}
    foreach ($scenario in @($scenarios | Where-Object { $_.action -eq 'object-visual-orbit' })) {
        $orbitAnchorsByKey["$($scenario.level)|$($scenario.taskId)"] = $scenario
    }
    foreach ($level in $levels) {
        foreach ($entry in @(Values $level.inventory)) {
            $hasModel = -not [string]::IsNullOrWhiteSpace([string]$entry.modelId)
            $posCount = @($entry.authoredPosition).Count
            $rotCount = @($entry.authoredRotation).Count
            $expectedRenderable = ($hasModel -and $posCount -eq 3)
            $declaredRenderable = [bool]$entry.renderable
            Require ($declaredRenderable -eq $expectedRenderable) "Level $($level.level) task $($entry.taskId) renderable flag $declaredRenderable disagrees with fields (model=$hasModel pos=$posCount rot=$rotCount)."
            Require ($null -ne $entry.hasModel -and $null -ne $entry.hasPosition -and $null -ne $entry.hasRotation) "Level $($level.level) task $($entry.taskId) is missing renderable classification fields."
            if ($declaredRenderable) {
                $orbitScenario = $orbitAnchorsByKey["$($level.level)|$($entry.taskId)"]
                Require ($null -ne $orbitScenario) "Level $($level.level) renderable task $($entry.taskId) must have exactly one object-visual-orbit scenario; found 0."
                $anchor = $orbitScenario.anchor
                Require ([string]$anchor.modelId -eq [string]$entry.modelId) "Level $($level.level) task $($entry.taskId) orbit anchor modelId does not match inventory."
                Require (@($anchor.views).Count -eq 10) "Level $($level.level) task $($entry.taskId) orbit anchor must carry ten views."
                foreach ($view in @('front','back','left','right','top','bottom','front-left','front-right','back-left','back-right')) {
                    Require (@($anchor.views) -contains $view) "Level $($level.level) task $($entry.taskId) orbit anchor is missing view '$view'."
                }
                $positionDelta = Compare-Object -ReferenceObject @($entry.authoredPosition) -DifferenceObject @($anchor.authoredPosition)
                Require ($null -eq $positionDelta -or @($positionDelta).Count -eq 0) "Level $($level.level) task $($entry.taskId) orbit anchor position differs from inventory."
                $lodDelta = Compare-Object -ReferenceObject @($entry.lods) -DifferenceObject @($anchor.lods)
                Require ($null -eq $lodDelta -or @($lodDelta).Count -eq 0) "Level $($level.level) task $($entry.taskId) orbit anchor LODs differ from inventory."
            }
        }
    }
    $renderableCount = @($levels | ForEach-Object { @(Values $_.inventory | Where-Object { $_.renderable }) }).Count
    Require ($orbitAnchorsByKey.Count -eq $renderableCount) "Orbit anchor count $($orbitAnchorsByKey.Count) must equal renderable inventory count $renderableCount."

    # ---- Runner evidence/reversible primitives contract (Task 2) ----
    # Every case below is validated by editor-e2e.ps1 -ValidateOnly BEFORE any
    # editor launch.  Each scenario is otherwise well-formed (carries both a UI
    # oracle and a state oracle) and carries exactly one defect, so the runner
    # must reject it for that specific reason; the two pair cases prove the
    # generic screenshot/state-pair rule, and the valid cases must pass.

    $invalidOrbitAngle = Join-Path $runnerFixtureRoot 'invalid-orbit-angle.json'
    New-RunnerFixtureFile $invalidOrbitAngle @{ schemaVersion=1; scenarios=@(
        @{ name='runner-invalid-orbit-angle'; level=1; requiresMutation=$false; steps=@(
            @{ id='launch'; type='launch_editor'; level=1 }
            @{ id='window'; type='wait_for_window'; timeoutSeconds=45 }
            @{ id='shot'; type='screenshot'; name='primitive-shot' }
            @{ id='orbit'; type='orbit_camera'; angle='sideways'; distance=200; pixels=12; screenshotAfter='orbit-shot' }
            @{ id='close'; type='close_editor'; force=$true }
        ) }
    ) }

    $invalidOrbitDistance = Join-Path $runnerFixtureRoot 'invalid-orbit-distance.json'
    New-RunnerFixtureFile $invalidOrbitDistance @{ schemaVersion=1; scenarios=@(
        @{ name='runner-invalid-orbit-distance'; level=1; requiresMutation=$false; steps=@(
            @{ id='launch'; type='launch_editor'; level=1 }
            @{ id='window'; type='wait_for_window'; timeoutSeconds=45 }
            @{ id='shot'; type='screenshot'; name='primitive-shot' }
            @{ id='orbit'; type='orbit_camera'; angle='front'; distance=-5; pixels=12; screenshotAfter='orbit-shot' }
            @{ id='close'; type='close_editor'; force=$true }
        ) }
    ) }

    $outsidePath = Join-Path $runnerFixtureRoot 'outside-path.json'
    New-RunnerFixtureFile $outsidePath @{ schemaVersion=1; scenarios=@(
        @{ name='runner-path-outside-game-root'; level=1; requiresMutation=$true; steps=@(
            @{ id='launch'; type='launch_editor'; level=1 }
            @{ id='window'; type='wait_for_window'; timeoutSeconds=45 }
            @{ id='shot'; type='screenshot'; name='primitive-shot' }
            @{ id='snapshot'; type='snapshot_paths'; paths=@('C:\Windows\win.ini') }
            @{ id='restore'; type='restore_paths' }
            @{ id='close'; type='close_editor'; force=$true }
        ) }
    ) }

    $escapePath = Join-Path $runnerFixtureRoot 'escape-path.json'
    New-RunnerFixtureFile $escapePath @{ schemaVersion=1; scenarios=@(
        @{ name='runner-path-escape-attempt'; level=1; requiresMutation=$true; steps=@(
            @{ id='launch'; type='launch_editor'; level=1 }
            @{ id='window'; type='wait_for_window'; timeoutSeconds=45 }
            @{ id='shot'; type='screenshot'; name='primitive-shot' }
            @{ id='snapshot'; type='snapshot_paths'; paths=@('..\..\Windows\win.ini') }
            @{ id='restore'; type='restore_paths' }
            @{ id='close'; type='close_editor'; force=$true }
        ) }
    ) }

    $missingRestoreHashes = Join-Path $runnerFixtureRoot 'missing-restore-hashes.json'
    New-RunnerFixtureFile $missingRestoreHashes @{ schemaVersion=1; scenarios=@(
        @{ name='runner-restore-without-snapshot'; level=1; requiresMutation=$true; steps=@(
            @{ id='launch'; type='launch_editor'; level=1 }
            @{ id='window'; type='wait_for_window'; timeoutSeconds=45 }
            @{ id='shot'; type='screenshot'; name='primitive-shot' }
            @{ id='restore'; type='restore_paths'; paths=@('editor/qed/qedconfig.qsc') }
            @{ id='close'; type='close_editor'; force=$true }
        ) }
    ) }

    $invalidAssertFileHash = Join-Path $runnerFixtureRoot 'invalid-assert-file-hash.json'
    New-RunnerFixtureFile $invalidAssertFileHash @{ schemaVersion=1; scenarios=@(
        @{ name='runner-invalid-file-hash'; level=1; requiresMutation=$false; steps=@(
            @{ id='launch'; type='launch_editor'; level=1 }
            @{ id='window'; type='wait_for_window'; timeoutSeconds=45 }
            @{ id='shot'; type='screenshot'; name='primitive-shot' }
            @{ id='hash-check'; type='assert_file_hash'; path='editor/qed/qedconfig.qsc'; sha256='not-a-hex-digest' }
            @{ id='close'; type='close_editor'; force=$true }
        ) }
    ) }

    $failedStateOnly = Join-Path $runnerFixtureRoot 'failed-state-only.json'
    New-RunnerFixtureFile $failedStateOnly @{ schemaVersion=1; scenarios=@(
        @{ name='runner-state-only-pair'; level=1; requiresMutation=$false; steps=@(
            @{ id='launch'; type='launch_editor'; level=1 }
            @{ id='window'; type='wait_for_window'; timeoutSeconds=45 }
            @{ id='healthy'; type='capture_window_state' }
            @{ id='close'; type='close_editor'; force=$true }
        ) }
    ) }

    $failedVisualOnly = Join-Path $runnerFixtureRoot 'failed-visual-only.json'
    New-RunnerFixtureFile $failedVisualOnly @{ schemaVersion=1; scenarios=@(
        @{ name='runner-visual-only-pair'; level=1; requiresMutation=$false; steps=@(
            @{ id='launch'; type='launch_editor'; level=1 }
            @{ id='window'; type='wait_for_window'; timeoutSeconds=45 }
            @{ id='shot'; type='screenshot'; name='primitive-pair-shot' }
            @{ id='close'; type='close_editor'; force=$true }
        ) }
    ) }

    $validPair = Join-Path $runnerFixtureRoot 'valid-pair.json'
    New-RunnerFixtureFile $validPair @{ schemaVersion=1; scenarios=@(
        @{ name='runner-screenshot-state-valid-pair'; level=1; requiresMutation=$false; steps=@(
            @{ id='launch'; type='launch_editor'; level=1 }
            @{ id='window'; type='wait_for_window'; timeoutSeconds=45 }
            @{ id='shot'; type='screenshot'; name='primitive-pair-shot' }
            @{ id='healthy'; type='capture_window_state' }
            @{ id='state-shot'; type='screenshot'; name='primitive-pair-state-shot' }
            @{ id='close'; type='close_editor'; force=$true }
        ) }
    ) }

    $validMutation = Join-Path $runnerFixtureRoot 'valid-mutation.json'
    New-RunnerFixtureFile $validMutation @{ schemaVersion=1; scenarios=@(
        @{ name='runner-valid-reversible-mutation'; level=1; requiresMutation=$true; steps=@(
            @{ id='launch'; type='launch_editor'; level=1 }
            @{ id='window'; type='wait_for_window'; timeoutSeconds=45 }
            @{ id='shot'; type='screenshot'; name='primitive-shot' }
            @{ id='snapshot'; type='snapshot_paths'; paths=@('editor/qed/qedconfig.qsc') }
            @{ id='restore'; type='restore_paths' }
            @{ id='close'; type='close_editor'; force=$true }
        ) }
    ) }

    $runnerCases = @(
        @{ Label='unsupported orbit angle'; Path=$invalidOrbitAngle; Code=1; Keyword='angle must be one of' },
        @{ Label='unsupported orbit distance'; Path=$invalidOrbitDistance; Code=1; Keyword='distance must be greater than 0' },
        @{ Label='path outside D:\IGI1'; Path=$outsidePath; Code=1; Keyword='relative to the game root' },
        @{ Label='path escaping D:\IGI1'; Path=$escapePath; Code=1; Keyword='relative to the game root' },
        @{ Label='missing restore hashes'; Path=$missingRestoreHashes; Code=1; Keyword='no preceding snapshot_paths' },
        @{ Label='invalid file-hash digest'; Path=$invalidAssertFileHash; Code=1; Keyword='SHA-256 digest' },
        @{ Label='failed state-only pair'; Path=$failedStateOnly; Code=1; Keyword='screenshot/UI oracle' },
        @{ Label='failed visual-only pair'; Path=$failedVisualOnly; Code=1; Keyword='state/data oracle' },
        @{ Label='valid screenshot/state pair'; Path=$validPair; Code=0; Keyword='Validated' },
        @{ Label='valid reversible mutation manifest'; Path=$validMutation; Code=0; Keyword='Validated' }
    )
    foreach ($case in $runnerCases) {
        $outcome = Invoke-RunnerValidate $case.Path
        Require ($outcome.ExitCode -eq $case.Code) "Runner contract case '$($case.Label)' exited $($outcome.ExitCode); expected $($case.Code). Output: $($outcome.Output)"
        Assert-OutputContains $outcome.Output $case.Keyword $case.Label
    }

    Write-Host 'Editor workflow manifest contract: PASS'
} finally {
    if ($locationPushed) { Pop-Location }
    Remove-Item -LiteralPath $manifestPath -Force -ErrorAction SilentlyContinue
    Remove-Item -LiteralPath $decompileRoot -Recurse -Force -ErrorAction SilentlyContinue
    Remove-Item -LiteralPath $runnerFixtureRoot -Recurse -Force -ErrorAction SilentlyContinue
}
