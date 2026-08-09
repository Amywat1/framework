# Batch-format C/H sources using .clang-format in repo root.
# 按需手动执行，不纳入 check_all 门禁。
# Usage:
#   .\scripts\format.ps1           # format all files in place
#   .\scripts\format.ps1 -Check    # check only, do not modify

param(
    [switch]$Check
)

$ErrorActionPreference = "Stop"

$Root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
Set-Location $Root

$ClangFormatExe = $null
$ClangFormatCmd = Get-Command clang-format -ErrorAction SilentlyContinue
if ($ClangFormatCmd) {
    $ClangFormatExe = $ClangFormatCmd.Source
} elseif (Test-Path "C:\Program Files\LLVM\bin\clang-format.exe") {
    $ClangFormatExe = "C:\Program Files\LLVM\bin\clang-format.exe"
}

if (-not $ClangFormatExe) {
    Write-Error @"
clang-format not found. Install LLVM first:
  winget install LLVM.LLVM
If still unavailable, reopen the terminal or add to PATH:
  C:\Program Files\LLVM\bin
Or use WSL: wsl bash ./scripts/format.sh
"@
}

$ExcludePattern = '\\(build|third_party|\.cache|\.git)\\'
$Files = Get-ChildItem -Path $Root -Recurse -Include *.c, *.h -File |
    Where-Object { $_.FullName -notmatch $ExcludePattern }

$Files = $Files | Sort-Object FullName

if ($Files.Count -eq 0) {
    Write-Host "No .c/.h files found."
    exit 0
}

Write-Host "clang-format: $(& $ClangFormatExe --version | Select-Object -First 1)"
Write-Host "config:       $Root\.clang-format"
Write-Host "file count:   $($Files.Count)"
Write-Host ""

if ($Check) {
    $Fail = 0
    foreach ($File in $Files) {
        & $ClangFormatExe --dry-run --Werror $File.FullName 2>$null
        if ($LASTEXITCODE -ne 0) {
            Write-Host "needs format: $($File.FullName)"
            $Fail = 1
        }
    }
    if ($Fail -eq 0) {
        Write-Host "All files are formatted."
        exit 0
    }
    Write-Error "Some files need formatting. Run: .\scripts\format.ps1"
}

foreach ($File in $Files) {
    $Rel = $File.FullName.Substring($Root.Length + 1)
    Write-Host "format: $Rel"
    & $ClangFormatExe -i $File.FullName
}

Write-Host ""
Write-Host "Done. Formatted $($Files.Count) file(s)."
