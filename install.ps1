# Installs (or updates) pomo on Windows for the current user, without admin rights:
#   irm https://raw.githubusercontent.com/ClaudeDebussy/cli-pomodoro/main/install.ps1 | iex
# Downloads pomo.exe from the latest GitHub release into
# %LOCALAPPDATA%\Programs\pomo and adds that folder to your PATH.
$ErrorActionPreference = 'Stop'

$repo = 'ClaudeDebussy/cli-pomodoro'
$dir = Join-Path $env:LOCALAPPDATA 'Programs\pomo'

Write-Host '==> Downloading pomo.exe'
New-Item -ItemType Directory -Force -Path $dir | Out-Null
Invoke-WebRequest "https://github.com/$repo/releases/latest/download/pomo.exe" -OutFile (Join-Path $dir 'pomo.exe')

$userPath = [Environment]::GetEnvironmentVariable('Path', 'User')
if (($userPath -split ';') -notcontains $dir) {
    Write-Host "==> Adding $dir to your PATH"
    [Environment]::SetEnvironmentVariable('Path', "$userPath;$dir".TrimStart(';'), 'User')
    Write-Host 'Open a new terminal so it picks up the new PATH.'
}

Write-Host '==> Done. Run: pomo'
