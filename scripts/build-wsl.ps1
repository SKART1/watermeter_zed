[CmdletBinding()]
param(
    [ValidateNotNullOrEmpty()]
    [string] $Distribution = 'Ubuntu',

    [ValidatePattern('^/[A-Za-z0-9_./+-]+$')]
    [string] $ToolchainPrefix,

    [switch] $CheckOnly,

    [string] $LogPath
)

$ErrorActionPreference = 'Stop'
$taskProjectRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
$taskWsl = (Get-Command wsl.exe -ErrorAction Stop).Source

if (-not (Test-Path -LiteralPath (Join-Path $taskProjectRoot 'makefile'))) {
    throw "Upstream makefile not found in $taskProjectRoot"
}

$taskLinuxRootOutput = @(& $taskWsl --distribution $Distribution --exec wslpath -a -u $taskProjectRoot)
if ($LASTEXITCODE -ne 0) {
    throw "Cannot resolve the project path in WSL distribution '$Distribution'."
}
$taskLinuxRoot = ($taskLinuxRootOutput -join "`n").Trim()
if (-not $taskLinuxRoot.StartsWith('/') -or $taskLinuxRoot.Contains("`n")) {
    throw 'wslpath returned an unexpected project path.'
}

Write-Host "WSL distribution: $Distribution"
Write-Host "Linux project: $taskLinuxRoot"
# Project-local host tools take precedence without changing the distribution or profile.
function Invoke-TaskWsl {
    param([string[]] $CommandArguments)
    & $taskWsl --distribution $Distribution --cd $taskLinuxRoot --exec sh -c 'PATH="$1:$PATH"; export PATH; shift; exec "$@"' build-wsl ($taskLinuxRoot + '/build/tools/host/usr/bin') @CommandArguments
    $script:taskCommandExit = $LASTEXITCODE
}

$taskCheckCommand = 'missing=0; for task_command in make python3 git sed awk cp rm mkdir; do if command -v "$task_command"; then :; else printf "MISSING: %s\n" "$task_command" >&2; missing=1; fi; done; exit "$missing"'
Invoke-TaskWsl -CommandArguments @('sh', '-c', $taskCheckCommand)
$taskPrerequisitesExit = $taskCommandExit

if ($ToolchainPrefix) {
    $taskCompilerExit = 0
    foreach ($taskTool in @('gcc', 'as', 'ld', 'nm', 'objcopy', 'objdump', 'ar', 'size')) {
        Invoke-TaskWsl -CommandArguments @(($ToolchainPrefix + '-elf-' + $taskTool), '--version')
        if ($taskCommandExit -ne 0) { $taskCompilerExit = 1 }
    }
} else {
    Write-Host 'TC32 prefix is not configured. Pass a Linux prefix such as /opt/tc32/bin/tc32.'
    $taskCompilerExit = 1
}

if ($CheckOnly) {
    if ($taskPrerequisitesExit -ne 0 -or $taskCompilerExit -ne 0) {
        Write-Host 'Build prerequisites are incomplete; no build was started.'
        exit 1
    }
    Write-Host 'Build prerequisites are available; no build was started.'
    exit 0
}

if ($taskPrerequisitesExit -ne 0 -or $taskCompilerExit -ne 0) {
    throw 'Build prerequisites are incomplete. Configure make, Python 3, Git and a Linux TC32 compiler first.'
}

$taskReferenceRoot = Join-Path $taskProjectRoot 'build\reference'
$taskManifestPath = Join-Path $taskReferenceRoot 'manifest.json'
if (-not (Test-Path -LiteralPath $taskManifestPath)) {
    throw 'Save the bundled firmware images and hashes to build/reference/manifest.json before building; upstream clean targets modify bin/.'
}
$taskManifest = Get-Content -Raw -LiteralPath $taskManifestPath | ConvertFrom-Json
if (@($taskManifest.images).Count -lt 1) { throw 'Reference manifest contains no images.' }
foreach ($taskImage in $taskManifest.images) {
    if (-not $taskImage.file -or [IO.Path]::GetFileName($taskImage.file) -cne $taskImage.file -or $taskImage.file.Contains('\') -or $taskImage.file.Contains('/')) {
        throw 'Invalid reference image name.'
    }
    $taskReferencePath = Join-Path $taskReferenceRoot $taskImage.file
    if ((Get-Item -LiteralPath $taskReferencePath).Length -ne $taskImage.size -or (Get-FileHash -Algorithm SHA256 -LiteralPath $taskReferencePath).Hash -ine $taskImage.sha256) {
        throw "Reference image validation failed: $($taskImage.file)"
    }
}
$taskTrackedImages = @(git -C $taskProjectRoot ls-files -- bin)
if ($LASTEXITCODE -ne 0) { throw 'Cannot enumerate bundled images.' }
foreach ($taskTrackedImage in $taskTrackedImages) {
    if ([IO.Path]::GetFileName($taskTrackedImage) -cnotin @($taskManifest.images.file)) {
        throw "Bundled image has no verified backup: $taskTrackedImage"
    }
}

if (-not $LogPath) {
    $LogPath = Join-Path $taskProjectRoot ('build\logs\baseline-' + [DateTime]::UtcNow.ToString('yyyyMMdd-HHmmss') + '.log')
}
$taskLogFullPath = [IO.Path]::GetFullPath($LogPath)
New-Item -ItemType Directory -Force -Path (Split-Path -Parent $taskLogFullPath) | Out-Null
if (Test-Path -LiteralPath $taskLogFullPath) { throw "Build log already exists: $taskLogFullPath" }
@("UTC: $([DateTimeOffset]::UtcNow.ToString('o'))", "Source commit: $(git -C $taskProjectRoot rev-parse HEAD)", "Toolchain: $ToolchainPrefix", "WSL: $Distribution", "Project: $taskLinuxRoot") | Set-Content -LiteralPath $taskLogFullPath -Encoding utf8
# Full clean removes stale SDK objects as well; main-build's clean-project does not.
# Fixed serial targets only. Flash/erase/reset targets are never accepted here.
foreach ($taskTarget in @('clean', 'all')) {
    "make -j1 $taskTarget COMPILE_PREFIX=$ToolchainPrefix" | Add-Content -LiteralPath $taskLogFullPath
    Invoke-TaskWsl -CommandArguments @('make', '-j1', $taskTarget, ('COMPILE_PREFIX=' + $ToolchainPrefix)) 2>&1 | Tee-Object -FilePath $taskLogFullPath -Append
    "Exit code: $taskCommandExit" | Add-Content -LiteralPath $taskLogFullPath
    if ($taskCommandExit -ne 0) {
        throw "WSL make $taskTarget failed with exit code $taskCommandExit. Log: $taskLogFullPath"
    }
}
Write-Host "Build log: $taskLogFullPath"
