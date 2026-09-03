$ErrorActionPreference = 'Stop'

$root = 'D:\IGI1'
$generator = Join-Path $PSScriptRoot 'New-EditorCorpusManifest.ps1'
$runner = Join-Path $PSScriptRoot 'editor-e2e.ps1'
$output = Join-Path $env:TEMP ('igi-editor-corpus-contract-' + [guid]::NewGuid().ToString('N') + '.json')

if (-not (Test-Path -LiteralPath $generator)) {
    throw "Corpus manifest generator is missing: $generator"
}

try {
    & pwsh -NoProfile -ExecutionPolicy Bypass -File $generator -GameRoot $root -OutputPath $output
    if ($LASTEXITCODE -ne 0) { throw "Corpus manifest generator failed with exit code $LASTEXITCODE." }
    if (-not (Test-Path -LiteralPath $output)) { throw "Generator did not write $output." }

    $manifest = Get-Content -LiteralPath $output -Raw | ConvertFrom-Json
    $scenarios = @($manifest.scenarios)
    if ($scenarios.Count -ne 28) { throw "Expected 28 level scenarios, got $($scenarios.Count)." }
    $levels = @($scenarios | ForEach-Object { [int]$_.level } | Sort-Object)
    if (($levels -join ',') -ne '1,1,2,2,3,3,4,4,5,5,6,6,7,7,8,8,9,9,10,10,11,11,12,12,13,13,14,14') {
        throw "Generated level coverage was '$($levels -join ',')'."
    }

    foreach ($level in 1..14) {
        $terrainScenario = @($scenarios | Where-Object { $_.name -eq "level$level-terrain-shortcut" })
        if ($terrainScenario.Count -ne 1) { throw "Level $level is missing terrain shortcut coverage." }
        $terrainTypes = @($terrainScenario[0].steps | ForEach-Object { [string]$_.type })
        foreach ($required in @('launch_editor','wait_for_log','assert_process','key','screenshot','assert_screenshot_color_ratio','close_editor')) {
            if ($terrainTypes -notcontains $required) { throw "Level $level terrain shortcut is missing $required coverage." }
        }
    }

    foreach ($scenario in @($scenarios | Where-Object { $_.name -like '*-corpus-load-render' })) {
        $types = @($scenario.steps | ForEach-Object { [string]$_.type })
        foreach ($required in @('launch_editor','wait_for_log','assert_process','assert_path','screenshot','assert_screenshot_region','assert_cursor_visible','assert_cursor_hidden','close_editor')) {
            if ($types -notcontains $required) { throw "Level $($scenario.level) is missing $required coverage." }
        }
        if (@($scenario.corpusFiles).Count -lt 6) { throw "Level $($scenario.level) is missing required corpus inventory entries." }
        if ($types -notcontains 'assert_log') { throw "Level $($scenario.level) has no log assertions." }
        $sceneCheck = @($scenario.steps | Where-Object { $_.id -eq 'scene-visible' })
        if ($sceneCheck.Count -ne 1) { throw "Level $($scenario.level) must have exactly one scene-visible check." }
        if ($null -ne $sceneCheck[0].minUniqueRatio) { throw "Level $($scenario.level) must not use generic minUniqueRatio rendering gates." }
        $cursorAfterPause = @($scenario.steps | Where-Object { $_.id -eq 'cursor-after-pause' })
        if ($cursorAfterPause.Count -ne 1 -or $cursorAfterPause[0].type -ne 'assert_cursor_hidden') {
            throw "Level $($scenario.level) must verify the custom cursor resumes after pause."
        }
        $pauseCheck = @($scenario.steps | Where-Object { $_.id -eq 'pause-visible' })
        if ($pauseCheck.Count -ne 1 -or $null -ne $pauseCheck[0].minUniqueRatio) {
            throw "Level $($scenario.level) must not use generic minUniqueRatio pause gates."
        }
    }

    $runnerSource = Get-Content -LiteralPath $runner -Raw
    if ($runnerSource -match '\$closed\.exitCode\s+-ne\s+0' -and $runnerSource -notmatch '\$null\s+-ne\s+\$closed\.exitCode') {
        throw 'Graceful-close handling must only reject an explicitly non-zero exit code.'
    }

    & pwsh -NoProfile -ExecutionPolicy Bypass -File $runner -ValidateOnly -ScenarioPath $output
    if ($LASTEXITCODE -ne 0) { throw "Generated corpus manifest was rejected by the runner." }
    Write-Host 'Editor E2E corpus contract: PASS'
} finally {
    Remove-Item -LiteralPath $output -Force -ErrorAction SilentlyContinue
}
