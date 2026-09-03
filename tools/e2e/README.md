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

The current manifest exercises Level 9 weather, Level 12 startup glass,
pause logging/severity, property-panel interaction, save/reopen persistence,
and model import with model-specific texture application evidence. The model
scenario also captures the property field before and after typing so a failed
import can be diagnosed from UI evidence rather than a passing unit test.
