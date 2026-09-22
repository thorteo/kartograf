#!/usr/bin/env bash
# Installs the Windows build for everyday use: copies the overlay out of the
# WSL build tree into %LOCALAPPDATA%\Programs\tarkov-help and puts a shortcut
# on the Windows desktop.
#
# A copy rather than a shortcut into the build tree: a \\wsl.localhost path
# only resolves while WSL is running, and the build tree holds a half-linked
# exe during every rebuild. Run again after a build to update the copy.
#
#   cmake --build --preset windows-dev && scripts/install.sh
set -euo pipefail

root="$(cd "$(dirname "$0")/.." && pwd)"
exe="$root/build/windows-dev/tarkov_help.exe"
[ -f "$exe" ] || { echo "no build at $exe; run cmake --build --preset windows-dev" >&2; exit 1; }

localappdata="$(wslpath "$(cmd.exe /c 'echo %LOCALAPPDATA%' 2>/dev/null | tr -d '\r')")"
dest="$localappdata/Programs/tarkov-help"
mkdir -p "$dest"

# Windows will not overwrite a running exe, but it will rename one. Move the
# old copy aside so an update works while the overlay is open; the running
# instance keeps going and the next start picks up the new file.
if [ -f "$dest/tarkov_help.exe" ]; then
    rm -f "$dest/tarkov_help.old.exe" 2>/dev/null || true
    mv "$dest/tarkov_help.exe" "$dest/tarkov_help.old.exe"
fi
cp "$exe" "$dest/tarkov_help.exe"
echo "installed $(wslpath -w "$dest/tarkov_help.exe")"

# The shortcut. WScript.Shell resolves the real desktop, OneDrive-redirected
# or not.
timeout 60 powershell.exe -NoProfile -Command "
  \$desktop = [Environment]::GetFolderPath('Desktop');
  \$link = (New-Object -ComObject WScript.Shell).CreateShortcut((Join-Path \$desktop 'Tarkov Help.lnk'));
  \$link.TargetPath = '$(wslpath -w "$dest/tarkov_help.exe")';
  \$link.WorkingDirectory = '$(wslpath -w "$dest")';
  \$link.Description = 'Escape from Tarkov overlay';
  \$link.Save();
  Write-Output ('shortcut ' + (Join-Path \$desktop 'Tarkov Help.lnk'))"
