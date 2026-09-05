function Test-SmartModelLog {
    param([string]$Text, $Anchor)
    $failures = [Collections.Generic.List[string]]::new()
    if (@($Anchor.authoredPosition).Count -ne 3 -or @($Anchor.authoredRotation).Count -ne 3 -or [string]::IsNullOrWhiteSpace([string]$Anchor.modelId)) { throw 'Incomplete authored model anchor.' }
    $model = [regex]::Escape([string]$Anchor.modelId)
    $rows = [regex]::Matches($Text, ('\[LevelLoader\] Object Loaded: ModelID='+$model+', Type=([^,]+), Name=(.*?), Pos=\(([^)]+)\), Ori=\(([^)]+)\),'))
    $matching = @()
    foreach ($row in $rows) {
        $pos = @($row.Groups[3].Value.Split(',') | ForEach-Object { [double]::Parse($_,[Globalization.CultureInfo]::InvariantCulture) })
        $ori = @($row.Groups[4].Value.Split(',') | ForEach-Object { [double]::Parse($_,[Globalization.CultureInfo]::InvariantCulture) })
        if ($pos.Count -ne 3 -or $ori.Count -ne 3) { continue }
        $same = $row.Groups[1].Value -eq $Anchor.type
        foreach ($axis in 0..2) {
            foreach ($value in @($pos[$axis],$ori[$axis],[double]$Anchor.authoredPosition[$axis],[double]$Anchor.authoredRotation[$axis])) {
                if ([double]::IsNaN($value) -or [double]::IsInfinity($value)) { $same=$false }
            }
            if ([Math]::Abs($pos[$axis]-[double]$Anchor.authoredPosition[$axis]) -gt 32 -or [Math]::Abs($ori[$axis]-[double]$Anchor.authoredRotation[$axis]) -gt 0.00001) { $same=$false }
        }
        if ($same) { $matching += $row.Value }
    }
    if ($matching.Count -ne 1) { $failures.Add('Expected one matching loaded-model transform; absent or ambiguous evidence.') }
    $assignment = [regex]::Matches($Text, ('\[TEX Native\] Applied textures to modelId='+$model+' subMeshes=(\d+) datTextures=(\d+) assigned=(\d+)'))
    if ($assignment.Count -eq 0) { $failures.Add('Missing live texture-assignment evidence.') }
    foreach ($row in $assignment) {
        if ([int]$row.Groups[1].Value -le 0 -or [int]$row.Groups[2].Value -le 0 -or [int]$row.Groups[3].Value -ne [int]$row.Groups[1].Value) { $failures.Add('Incomplete live submesh texture assignments.') }
    }
    $textureLoads = @()
    foreach ($texture in @($Anchor.requiredTextures)) {
        if ([string]::IsNullOrWhiteSpace([string]$texture)) { continue }
        $pattern = '\[TEX\] ResCache loaded '+[regex]::Escape([string]$texture)+' ([1-9]\d*)x([1-9]\d*)(?:\s|$)'
        $loaded = [regex]::IsMatch($Text,$pattern)
        $textureLoads += [pscustomobject]@{texture=$texture;loaded=$loaded}
        if (-not $loaded) { $failures.Add("Missing successful load evidence for required texture $texture.") }
    }
    [pscustomobject]@{passed=($failures.Count -eq 0);failures=@($failures.ToArray());matchingTransforms=$matching.Count;assignmentRecords=$assignment.Count;requiredTextureLoads=$textureLoads;scope='loader transform, required texture loads and assignment counts; not per-draw GPU bindings or visual acceptance'}
}
