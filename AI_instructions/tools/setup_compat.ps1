<# Restore local Windows skill aliases; canonical files always stay in AI_instructions. #>
[CmdletBinding()]
param(
    [string]$WorkspaceRoot
)

$ErrorActionPreference = 'Stop'
$aiRoot = Split-Path -Parent $PSScriptRoot
$sourceRoot = Split-Path -Parent $aiRoot
if (-not $WorkspaceRoot) {
    $candidate = Split-Path -Parent (Split-Path -Parent (Split-Path -Parent $sourceRoot))
    if ((Split-Path -Leaf $sourceRoot) -eq 'src' -and
        (Split-Path -Leaf (Split-Path -Parent $sourceRoot)) -eq 'sp' -and
        (Test-Path -LiteralPath (Join-Path $candidate 'dialogsrc'))) {
        $WorkspaceRoot = $candidate
    }
}

function Add-DirectoryAlias([string]$Path, [string]$Target) {
    if (Test-Path -LiteralPath $Path) {
        $existing = Get-Item -LiteralPath $Path -Force
        if ($existing.LinkType -eq 'Junction' -and
            ([IO.Path]::GetFullPath([string]$existing.Target[0])).TrimEnd('\') -eq
            ([IO.Path]::GetFullPath($Target)).TrimEnd('\')) {
            Write-Host "OK: $Path"
        } else {
            Write-Warning "Existing path left in place; compare with canonical files: $Path"
        }
        return
    }
    $parent = Split-Path -Parent $Path
    New-Item -ItemType Directory -Path $parent -Force | Out-Null
    New-Item -ItemType Junction -Path $Path -Target $Target | Out-Null
    Write-Host "Linked: $Path"
}

$roots = @($sourceRoot)
if ($WorkspaceRoot -and ([IO.Path]::GetFullPath($WorkspaceRoot) -ne [IO.Path]::GetFullPath($sourceRoot))) {
    $roots += $WorkspaceRoot
    Add-DirectoryAlias (Join-Path $WorkspaceRoot 'AI_instructions') $aiRoot
}
foreach ($root in $roots) {
    Add-DirectoryAlias (Join-Path $root '.agents\skills') (Join-Path $aiRoot 'skills')
    foreach ($skill in Get-ChildItem -LiteralPath (Join-Path $aiRoot 'skills') -Directory) {
        Add-DirectoryAlias (Join-Path $root ('.claude\skills\' + $skill.Name)) $skill.FullName
    }
}
Write-Host 'Canonical Markdown and scripts remain in AI_instructions. No existing files were overwritten.'
