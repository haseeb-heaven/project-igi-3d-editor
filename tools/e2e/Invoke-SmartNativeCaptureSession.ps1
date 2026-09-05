[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$ArtifactsRoot,
    [string]$InventoryPath = 'artifacts/task6-metadata-manifest.json',
    [string]$GameRoot = 'D:\IGI1',
    [string]$EditorExePath = '',
    [ValidateRange(1,14)][int]$Level = 1,
    [string]$Category = 'All',
    [string[]]$IncludeTypes = @(),
    [ValidateRange(0,2147483647)][int]$MaxObjects = 0,
    [switch]$DistinctTypes,
    [switch]$DistinctCategories,
    [ValidateRange(1,10)][int]$ViewCount = 10,
    [switch]$PrepareOnly
)

$ErrorActionPreference = 'Stop'
$allCaptureViews = @('Ext_000','Ext_060','Ext_120','Ext_180','Ext_240','Ext_300','Int_000','Int_090','Int_180','Int_270')
$captureViews = @($allCaptureViews | Select-Object -First $ViewCount)
if (-not $PrepareOnly -and (Get-Process igi1ed -ErrorAction SilentlyContinue)) { throw 'Close the existing editor before a native capture session.' }
if ([string]::IsNullOrWhiteSpace($EditorExePath)) {
    $binCandidate = Join-Path $PSScriptRoot '..\..\bin\Release\igi1ed.exe'
    if (Test-Path -LiteralPath $binCandidate) {
        $EditorExePath = [IO.Path]::GetFullPath($binCandidate)
    } else {
        $EditorExePath = Join-Path $GameRoot 'igi1ed.exe'
    }
} else {
    $EditorExePath = [IO.Path]::GetFullPath($EditorExePath)
}
if (-not (Test-Path -LiteralPath $EditorExePath -PathType Leaf)) { throw "Editor executable not found: $EditorExePath" }
$logPath = Join-Path (Split-Path -Parent $EditorExePath) 'igi1ed.log'

function Get-PortableSha256([string]$Path) {
    $bytes = [IO.File]::ReadAllBytes($Path)
    $sha = [Security.Cryptography.SHA256]::Create()
    try { return ([BitConverter]::ToString($sha.ComputeHash($bytes))).Replace('-','').ToLowerInvariant() }
    finally { $sha.Dispose() }
}
function Read-SharedBytes([string]$Path, [Int64]$Offset = 0) {
    $stream = [IO.File]::Open($Path, [IO.FileMode]::Open, [IO.FileAccess]::Read, [IO.FileShare]::ReadWrite)
    try {
        $start = if ($Offset -gt $stream.Length) { 0 } else { $Offset }
        $remaining = $stream.Length - $start
        if ($remaining -gt 64MB) { throw "Fresh editor log segment is too large to evaluate safely: $remaining bytes." }
        $stream.Seek($start, [IO.SeekOrigin]::Begin) | Out-Null
        $bytes = [byte[]]::new([int]$remaining)
        $readOffset = 0
        while ($readOffset -lt $bytes.Length) {
            $read = $stream.Read($bytes, $readOffset, $bytes.Length - $readOffset)
            if ($read -le 0) { break }
            $readOffset += $read
        }
        if ($readOffset -eq 0) { Write-Output -NoEnumerate ([byte[]]::new(0)); return }
        if ($readOffset -eq $bytes.Length) { Write-Output -NoEnumerate $bytes; return }
        $actual = [byte[]]::new($readOffset)
        [Buffer]::BlockCopy($bytes, 0, $actual, 0, $readOffset)
        Write-Output -NoEnumerate $actual
    } finally { $stream.Dispose() }
}
function Get-SafeName([string]$Value) { return ($Value -replace '[^A-Za-z0-9_-]', '_') }
function Get-ObjectCategory([string]$Type) {
    if ($Type -in @('HumanSoldier','HumanSoldierFemale','HumanSoldierRPG','HumanPlayer','HumanAI','AISquad','PatrolPath','PatrolPathCommand')) { return 'AI' }
    if ($Type -in @('Car','Heli','Train','Plane','CarAI')) { return 'Vehicles' }
    if ($Type -in @('Building','Door','Terminal','Switch','AlarmControl','Elevator','Fence','Cabinet')) { return 'Buildings' }
    return 'RigidObjects'
}
function Get-RequiredTextureMap([string]$DatPath, [string]$OutputPath) {
    $converter = Join-Path $PSScriptRoot '../../assets/editor/tools/igi1conv/igi1conv.exe'
    if (-not (Test-Path -LiteralPath $converter)) { throw "Converter not found: $converter" }
    & $converter dat export $DatPath -o $OutputPath | Out-Null
    if ($LASTEXITCODE -ne 0 -or -not (Test-Path -LiteralPath $OutputPath)) { throw "DAT material export failed: $DatPath" }
    $map = @{}
    foreach ($model in @((Get-Content -LiteralPath $OutputPath -Raw | ConvertFrom-Json).models)) { $map[[string]$model.modelName] = @($model.textures) }
    return $map
}
function Wait-ForPattern([string]$Path, [Int64]$Offset, [string]$Pattern, [int]$TimeoutSeconds) {
    $deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
    do {
        if (Test-Path -LiteralPath $Path) {
            $bytes = Read-SharedBytes $Path $Offset
            if ($bytes.Length -gt 0 -and [Text.Encoding]::UTF8.GetString($bytes) -match $Pattern) { return $true }
        }
        Start-Sleep -Milliseconds 200
    } while ([DateTime]::UtcNow -lt $deadline)
    return $false
}
function Get-CommandPosition($Anchor) {
    $pos = @($Anchor.authoredPosition | ForEach-Object { [double]$_ })
    # The editor command accepts small coordinates as metres and scales them.
    # Pass the inverse only when all native coordinates would trigger that path.
    if (@($pos | Where-Object { [Math]::Abs($_) -ge 1000000 }).Count -eq 0) { return @($pos | ForEach-Object { $_ / 256.0 }) }
    return $pos
}
function Wait-ForCaptureFiles([string[]]$Paths, [int]$TimeoutSeconds = 180) {
    $deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
    do {
        $ready = $true
        foreach ($path in $Paths) {
            if (-not (Test-Path -LiteralPath $path -PathType Leaf) -or (Get-Item -LiteralPath $path).Length -le 0) { $ready = $false; break }
        }
        if ($ready) { return $true }
        Start-Sleep -Milliseconds 250
    } while ([DateTime]::UtcNow -lt $deadline)
    return $false
}
function Wait-ForCommandConsumed([string]$Path, [DateTime]$Started, [int]$TimeoutSeconds) {
    $deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
    do {
        if ((Test-Path -LiteralPath $Path -PathType Leaf) -and (Get-Item -LiteralPath $Path).Length -eq 0 -and (Get-Item -LiteralPath $Path).LastWriteTimeUtc -ge $Started.AddSeconds(-1)) { return $true }
        Start-Sleep -Milliseconds 200
    } while ([DateTime]::UtcNow -lt $deadline)
    return $false
}
function Close-Editor($Process) {
    if ($null -eq $Process) { return $true }
    try {
        if (-not $Process.HasExited) {
            Stop-Process -Id $Process.Id -Force -ErrorAction SilentlyContinue
            $Process.WaitForExit(3000) | Out-Null
        }
    } catch {}
    Start-Sleep -Milliseconds 500
    return $true
}

$ArtifactsRoot = [IO.Path]::GetFullPath($ArtifactsRoot)
if (Test-Path -LiteralPath $ArtifactsRoot) { throw 'Use a fresh artifact directory.' }
New-Item -ItemType Directory -Path $ArtifactsRoot -Force | Out-Null
$inventory = Get-Content -LiteralPath $InventoryPath -Raw | ConvertFrom-Json
$levelRow = @($inventory.levels | Where-Object level -eq $Level)
if ($levelRow.Count -ne 1) { throw "Missing or ambiguous inventory entry for level $Level." }
$levelRow = $levelRow[0]
$sourcePath = Join-Path $GameRoot ([string]$levelRow.sourcePath)
if (-not (Test-Path -LiteralPath $sourcePath)) { throw "Level source is missing: $sourcePath" }
$sourceHash = Get-PortableSha256 $sourcePath
$editorHash = Get-PortableSha256 $EditorExePath
$IncludeTypes = @($IncludeTypes | ForEach-Object { $_ -split ',' } | Where-Object { -not [string]::IsNullOrWhiteSpace($_) })
$all = @($levelRow.inventory)
if ($IncludeTypes.Count) { $all = @($all | Where-Object { $IncludeTypes -contains [string]$_.type }) }
$materialPath = Join-Path $ArtifactsRoot 'authored-materials.json'
$textureMap = Get-RequiredTextureMap (Join-Path $GameRoot "MISSIONS/location0/level$Level/level$Level.dat") $materialPath
$candidates = @($all | Where-Object {
    $_.modelId -and
    @($_.authoredPosition).Count -eq 3 -and
    (@($_.authoredRotation).Count -eq 3 -or (Get-ObjectCategory $_.type) -eq 'AI') -and
    $textureMap.ContainsKey([string]$_.modelId) -and
    @($textureMap[[string]$_.modelId]).Count -gt 0
})
if ($DistinctCategories) {
    $groupedByCat = $candidates | Group-Object { Get-ObjectCategory $_.type }
    $candidates = @($groupedByCat | ForEach-Object { $_.Group | Select-Object -First 1 })
} elseif ($DistinctTypes) {
    $candidates = @($candidates | Group-Object type | ForEach-Object { $_.Group | Select-Object -First 1 })
}
$candidates = @($candidates | Sort-Object {[string]$_.taskId}, {[string]$_.modelId})
$notSelected = @()
if ($MaxObjects -gt 0 -and $candidates.Count -gt $MaxObjects) {
    $notSelected = @($candidates | Select-Object -Skip $MaxObjects | ForEach-Object { [pscustomobject]@{taskId=[string]$_.taskId;type=[string]$_.type;category=(Get-ObjectCategory $_.type);modelId=[string]$_.modelId;status='NOT_SELECTED';reason="maximum object limit $MaxObjects reached"} })
    $candidates = @($candidates | Select-Object -First $MaxObjects)
}
$skipped = @($all | Where-Object {
    -not ($_.modelId -and @($_.authoredPosition).Count -eq 3 -and (@($_.authoredRotation).Count -eq 3 -or (Get-ObjectCategory $_.type) -eq 'AI') -and $textureMap.ContainsKey([string]$_.modelId) -and @($textureMap[[string]$_.modelId]).Count -gt 0)
} | ForEach-Object {
    $reason = if (-not $_.modelId) { 'non-renderable task' }
        elseif (@($_.authoredPosition).Count -ne 3) { 'missing authored position' }
        elseif (@($_.authoredRotation).Count -ne 3 -and (Get-ObjectCategory $_.type) -ne 'AI') { 'missing authored rotation' }
        else { 'no authored textures in level DAT' }
    [pscustomobject]@{taskId=[string]$_.taskId;type=[string]$_.type;category=(Get-ObjectCategory $_.type);modelId=$_.modelId;status='SKIPPED';reason=$reason}
}) + $notSelected
$plans = [Collections.Generic.List[object]]::new()
$index = 0
foreach ($anchor in $candidates) {
    $index++
    $model = [string]$anchor.modelId; $task = [string]$anchor.taskId
    $rot = @($anchor.authoredRotation)
    if ($rot.Count -lt 3 -and (Get-ObjectCategory $anchor.type) -eq 'AI') {
        $rot = @(0.0, 0.0, 6.28318)
    }
    $plans.Add([pscustomobject]@{index=$index;taskId=$task;type=[string]$anchor.type;category=(Get-ObjectCategory $anchor.type);modelId=$model;authoredPosition=@($anchor.authoredPosition);authoredRotation=$rot;requiredTextures=$(if($textureMap.ContainsKey($model)){@($textureMap[$model])}else{@()});prefix=('obj-{0:D4}-task{1}-{2}' -f $index,(Get-SafeName $task),(Get-SafeName $model))})
}
$statePath = Join-Path $ArtifactsRoot 'batch.json'
$state = [ordered]@{level=$Level;category=$Category;selectedTypes=@($IncludeTypes);sourcePath=$sourcePath;sourceSha256=$sourceHash;editorExecutable=$EditorExePath;editorSha256=$editorHash;logPath=$logPath;totalTasks=$all.Count;renderableCandidates=$candidates.Count;selectableObjects=$plans.Count;objects=@($plans);skippedTasks=@($skipped);launchCount=1;closeCount=1;viewCount=$captureViews.Count;screenshotsExpected=($plans.Count*$captureViews.Count);status=$(if($PrepareOnly){'PREPARED'}else{'NOT_RUN'})}
$state | ConvertTo-Json -Depth 15 | Set-Content -LiteralPath $statePath -Encoding UTF8
if ($PrepareOnly) { Write-Output "Prepared native one-session level ${Level}: $($plans.Count) objects, $($captureViews.Count) views each."; exit 0 }

$commandPath = Join-Path $GameRoot 'editor/tools/debug-command.txt'
$screenshotRoot = Join-Path $GameRoot 'screenshots'
$commandOriginal = if (Test-Path -LiteralPath $commandPath) { [IO.File]::ReadAllBytes($commandPath) } else { $null }
$fileBackups = @{}
$process = $null; $logOffset = if(Test-Path -LiteralPath $logPath){(Get-Item -LiteralPath $logPath).Length}else{0}
$runFailure = $null
try {
    $wmi = [wmiclass]'\\.\root\cimv2:Win32_Process'
    $created = $wmi.Create(('"' + $EditorExePath + '" --developer-mode --game-path "' + $GameRoot + '" -level ' + $Level), $GameRoot)
    if ([int]$created.ReturnValue -ne 0) { throw "WMI launch failed with return code $($created.ReturnValue)." }
    $process = Get-Process -Id ([int]$created.ProcessId) -ErrorAction Stop
    $startupDeadline = [DateTime]::UtcNow.AddSeconds(30)
    do {
        $process.Refresh()
        if ($process.SessionId -eq 1 -and $process.WorkingSet64 -gt 15MB) { break }
        Start-Sleep -Milliseconds 250
    } while ([DateTime]::UtcNow -lt $startupDeadline)
    if ($process.SessionId -ne 1) { throw 'Editor did not start in interactive Session 1.' }
    if (-not (Wait-ForPattern $logPath $logOffset ("\[App\] LoadLevel\(\) COMPLETE for level $Level") 300)) { throw "Level $Level did not load." }
    $respDeadline = [DateTime]::UtcNow.AddSeconds(30)
    do {
        $process.Refresh()
        if ($process.Responding) { break }
        Start-Sleep -Milliseconds 250
    } while ([DateTime]::UtcNow -lt $respDeadline)
    if (-not $process.Responding) { throw 'Editor did not become responsive after loading level.' }
    $probeStarted = [DateTime]::UtcNow
    [IO.File]::WriteAllText($commandPath, "goto level=$Level model=__smart_capture_probe_missing__" + [Environment]::NewLine, [Text.UTF8Encoding]::new($false))
    if (-not (Wait-ForCommandConsumed $commandPath $probeStarted 15)) { throw 'Selected editor does not consume developer commands. Use a build that supports --developer-mode and capture-model.' }
    New-Item -ItemType Directory -Path $screenshotRoot -Force | Out-Null
    foreach ($plan in $plans) {
        $allPaths = @($allCaptureViews | ForEach-Object { Join-Path $screenshotRoot ('Level{0:D2}_Model{1}_{2}.png' -f $Level,$plan.modelId,$_ ) })
        $paths = @($allPaths | Select-Object -First $captureViews.Count)
        foreach ($path in $allPaths) {
            if (-not $fileBackups.ContainsKey($path)) {
                $fileBackups[$path] = if(Test-Path -LiteralPath $path){[IO.File]::ReadAllBytes($path)}else{$null}
            }
            if (Test-Path -LiteralPath $path) { Remove-Item -LiteralPath $path -Force }
        }
        $pos = Get-CommandPosition $plan
        $line = ('capture-model level={0} model={1} x={2} y={3} z={4}' -f $Level,$plan.modelId,([double]$pos[0]).ToString('R',[Globalization.CultureInfo]::InvariantCulture),([double]$pos[1]).ToString('R',[Globalization.CultureInfo]::InvariantCulture),([double]$pos[2]).ToString('R',[Globalization.CultureInfo]::InvariantCulture))
        [IO.File]::WriteAllText($commandPath,$line + [Environment]::NewLine,[Text.UTF8Encoding]::new($false))
        if (-not (Wait-ForCaptureFiles $allPaths 180)) { throw "Native capture timed out for task $($plan.taskId), model $($plan.modelId)." }
        $targetDir = Join-Path $ArtifactsRoot ('screenshots\'+$plan.prefix)
        New-Item -ItemType Directory -Path $targetDir -Force | Out-Null
        foreach ($path in $paths) { Copy-Item -LiteralPath $path -Destination (Join-Path $targetDir ([IO.Path]::GetFileName($path))) -Force }
    }
} catch {
    $runFailure = $_.Exception.Message
} finally {
    $closed = Close-Editor $process
    if ($null -ne $commandOriginal) { [IO.File]::WriteAllBytes($commandPath,$commandOriginal) } elseif (Test-Path -LiteralPath $commandPath) { Remove-Item -LiteralPath $commandPath -Force }
    foreach ($path in $fileBackups.Keys) { if ($null -ne $fileBackups[$path]) { [IO.File]::WriteAllBytes($path,$fileBackups[$path]) } elseif (Test-Path -LiteralPath $path) { Remove-Item -LiteralPath $path -Force } }
}

if ($null -ne $runFailure) {
    $failure = [ordered]@{level=$Level;status='FAIL';failure=$runFailure;editorExecutable=$EditorExePath;editorSha256=$editorHash;logPath=$logPath;launchCount=1;closeCount=1;selectableObjects=$plans.Count;objects=@($plans);skippedTasks=@($skipped);screenshotsExpected=($plans.Count*$captureViews.Count);screenshotsCaptured=0;captureMethod='native direct camera requires a developer-capable editor build'}
    $failure | ConvertTo-Json -Depth 15 | Set-Content -LiteralPath $statePath -Encoding UTF8
    throw $runFailure
}

. (Join-Path $PSScriptRoot 'SmartModelEvidence.ps1')
$freshLog = if(Test-Path -LiteralPath $logPath){$bytes=Read-SharedBytes $logPath $logOffset;if($bytes.Length -gt 0){[Text.Encoding]::UTF8.GetString($bytes)}else{''}}else{''}
$evidence=@()
foreach($plan in $plans){
    $anchor=[pscustomobject]@{taskId=$plan.taskId;type=$plan.type;modelId=$plan.modelId;authoredPosition=$plan.authoredPosition;authoredRotation=$plan.authoredRotation;requiredTextures=$plan.requiredTextures}
    $item=Test-SmartModelLog $freshLog $anchor
    $shots=@(Get-ChildItem -LiteralPath (Join-Path $ArtifactsRoot ('screenshots\'+$plan.prefix)) -File -Filter '*.png' -ErrorAction SilentlyContinue)
    $item | Add-Member -NotePropertyName taskId -NotePropertyValue $plan.taskId -Force
    $item | Add-Member -NotePropertyName type -NotePropertyValue $plan.type -Force
    $item | Add-Member -NotePropertyName category -NotePropertyValue $plan.category -Force
    $item | Add-Member -NotePropertyName modelId -NotePropertyValue $plan.modelId -Force
    $item | Add-Member -NotePropertyName screenshotCount -NotePropertyValue $shots.Count -Force
    if($shots.Count -ne $captureViews.Count){$item.passed=$false;$item.failures=@($item.failures+"Expected $($captureViews.Count) screenshots, found $($shots.Count).")}
    if(@($plan.requiredTextures).Count -eq 0){$item.passed=$false;$item.failures=@($item.failures+'Required model texture list unavailable; do not infer a texture pass.')}
    $evidence+=$item
}
$failed=@($evidence|Where-Object{-not $_.passed}).Count
$final=[ordered]@{level=$Level;category=$Category;status=$(if($failed -eq 0){'PASS'}else{'FAIL'});editorExecutable=$EditorExePath;editorSha256=$editorHash;logPath=$logPath;launchCount=1;closeCount=1;objects=@($evidence);skippedTasks=@($skipped);screenshotsExpected=($plans.Count*$captureViews.Count);screenshotsCaptured=(@($evidence|Measure-Object screenshotCount -Sum).Sum);evidencePassed=(@($evidence|Where-Object passed).Count);evidenceFailed=$failed;captureMethod='native direct camera 6 exterior 60-degree views plus 4 interior views'}
$final|ConvertTo-Json -Depth 15|Set-Content -LiteralPath $statePath -Encoding UTF8
if($final.status -ne 'PASS'){exit 1}
Write-Output "PASS: native one-session capture, $($plans.Count) objects, $($final.screenshotsCaptured) screenshots."
