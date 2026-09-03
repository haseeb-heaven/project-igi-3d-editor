$ErrorActionPreference = 'Stop'

$runner = Join-Path $PSScriptRoot 'editor-e2e.ps1'
$valid = Join-Path $PSScriptRoot 'scenarios\editor-regression.json'
$invalid = Join-Path $PSScriptRoot 'scenarios\invalid-missing-step-id.json'

if (-not (Test-Path -LiteralPath $runner)) {
    throw "Runner is missing: $runner"
}

function Invoke-Validation([string] $manifest) {
    # Run the child out of process so its intentional malformed-manifest error
    # cannot be promoted by Windows PowerShell; the exit code remains
    # authoritative for both the invalid and valid contracts.
    $child = Start-Process -FilePath 'pwsh.exe' -ArgumentList @(
        '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', $runner,
        '-ValidateOnly', '-ScenarioPath', $manifest
    ) -WindowStyle Hidden -Wait -PassThru
    return [int]$child.ExitCode
}

$invalidCode = Invoke-Validation $invalid
if ($invalidCode -eq 0) {
    throw 'Malformed manifest was accepted.'
}

$validCode = Invoke-Validation $valid
if ($validCode -ne 0) {
    throw "Valid manifest was rejected with exit code $validCode."
}

Write-Host 'Editor E2E manifest contract: PASS'
