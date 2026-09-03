# Live editor E2E runner

`editor-e2e.ps1` drives the installed editor as a user would. It launches with
WMI from `D:\IGI1`, verifies the window is responsive in interactive Session 1,
sends explicit mouse/keyboard actions, captures PNG checkpoints, and writes
JSON/Markdown reports. The runner contains only generic step execution;
scenario intent, coordinates, expected files, and log patterns live in JSON.

From PowerShell:

```powershell
Set-Location D:\IGI1
$runner = 'D:\Code\project-igi-editor\tools\e2e\editor-e2e.ps1'
$manifest = 'D:\Code\project-igi-editor\tools\e2e\scenarios\editor-regression.json'

pwsh -NoProfile -ExecutionPolicy Bypass -File $runner -ScenarioPath $manifest -ValidateOnly
pwsh -NoProfile -ExecutionPolicy Bypass -File $runner -ScenarioPath $manifest -ScenarioName 'level*'
pwsh -NoProfile -ExecutionPolicy Bypass -File $runner -ScenarioPath $manifest -AllowGameDataMutation
```

Use `-ScenarioName` for a focused run, `-KeepEditorOpen` for manual inspection,
and `-ArtifactsRoot` for a known evidence directory. Mutation scenarios refuse
to run without `-AllowGameDataMutation`. Assertions against editor logs are
bounded to the current run's byte offset and poll until `timeoutSeconds`; file
assertions use the same bounded polling behavior. A failure captures
`failure.png` before cleanup and records the failed step in `scenario.json`.
The generic action vocabulary also includes `key_hold`; screenshot assertions
include region metrics, tolerant color ratios, and frame-to-frame difference
ratios. These assertions are data-driven and can be reused by additional
level/model fixtures without adding production-specific logic to the runner.

The focused manifest exercises Level 9 weather, Level 12 startup glass, pause
logging/severity, property-panel interaction, save/reopen persistence, and
model import with model-specific texture application evidence. The model
scenario also captures the property field before and after typing so a failed
import can be diagnosed from UI evidence rather than a passing unit test.

Generate the corpus matrix from the installed data and validate it without
launching a GUI:

```powershell
Set-Location D:\IGI1
$matrix = 'D:\Code\project-igi-editor\artifacts\e2e\corpus-matrix-manifest.json'
& 'D:\Code\project-igi-editor\tools\e2e\New-EditorCorpusManifest.ps1' -GameRoot D:\IGI1 -OutputPath $matrix
& 'D:\Code\project-igi-editor\tools\e2e\editor-e2e.ps1' -ValidateOnly -ScenarioPath $matrix
& 'D:\Code\project-igi-editor\tools\e2e\test-editor-corpus.ps1'
```

The generated matrix has one scenario per installed Level 1–14. Each scenario
checks load completion, required level archives, authored weather resolution,
texture-miss diagnostics, viewport rendering, pause-menu rendering, cursor
visibility, and graceful process exit. Run it serially with an explicit
artifacts directory:

```powershell
& 'D:\Code\project-igi-editor\tools\e2e\editor-e2e.ps1' `
  -GameRoot D:\IGI1 -ScenarioPath $matrix `
  -ArtifactsRoot 'D:\Code\project-igi-editor\artifacts\e2e\corpus-matrix-run'
```

The generated corpus scenarios are non-mutating. Keep the focused mutating
manifest behind `-AllowGameDataMutation`; do not run the two manifests in
parallel because both launch against the same installed editor state.

Run the serial regression pipeline after building/deploying the editor. It
validates both manifests, runs the existing focused workflows, then runs the
visual regressions that use real screenshots for terrain mode, indoor weather,
and distant building floors:

```powershell
& 'D:\Code\project-igi-editor\tools\e2e\test-editor-regression.ps1' `
  -GameRoot D:\IGI1 -AllowGameDataMutation
```

The pipeline is intentionally red while a known visual defect is present. A
red result is useful evidence only when its scenario directory contains the
failed step's screenshot and `scenario.json`; do not turn these assertions
into permissive scene-wide pixel checks.
