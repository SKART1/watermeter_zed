$ErrorActionPreference = 'Stop'
$taskRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
$taskReferenceRoot = Join-Path $taskRoot 'build\reference'
$taskManifestPath = Join-Path $taskReferenceRoot 'manifest.json'
if (Test-Path -LiteralPath $taskManifestPath) {
    throw 'Reference manifest already exists; keep the original backup and verify it with build-wsl.ps1.'
}
$taskHead = (git -C $taskRoot rev-parse HEAD).Trim()
if ($LASTEXITCODE -ne 0) { throw 'Cannot determine source commit.' }
$taskTrackedImages = @(git -C $taskRoot ls-files -- bin)
if ($LASTEXITCODE -ne 0 -or $taskTrackedImages.Count -lt 1) { throw 'No bundled images found.' }
# A later generated BIN must never be labelled as an upstream reference.
$taskBinChanges = @(git -C $taskRoot status --porcelain -- bin)
if ($LASTEXITCODE -ne 0 -or $taskBinChanges.Count -ne 0) { throw 'bin/ is modified; preserve and inspect it before saving references.' }
New-Item -ItemType Directory -Force -Path $taskReferenceRoot | Out-Null
$taskImages = foreach ($taskRelative in $taskTrackedImages) {
    $taskSource = Join-Path $taskRoot $taskRelative
    $taskName = [IO.Path]::GetFileName($taskRelative)
    $taskDestination = Join-Path $taskReferenceRoot $taskName
    if (Test-Path -LiteralPath $taskDestination) { throw "Reference already exists: $taskDestination" }
    Copy-Item -LiteralPath $taskSource -Destination $taskDestination
    $taskHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $taskSource).Hash.ToLowerInvariant()
    if ((Get-FileHash -Algorithm SHA256 -LiteralPath $taskDestination).Hash -ine $taskHash) { throw 'Reference copy hash mismatch.' }
    [ordered]@{file=$taskName;size=(Get-Item -LiteralPath $taskSource).Length;sha256=$taskHash}
}
[ordered]@{source_commit=$taskHead;captured_at=[DateTimeOffset]::UtcNow.ToString('o');source='bundled upstream bin/, not device readback';images=@($taskImages)} | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $taskManifestPath -Encoding utf8
Write-Host "Saved verified references: $taskManifestPath"
