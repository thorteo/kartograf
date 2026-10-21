#!/usr/bin/env bash
# Installs the Windows build for everyday use: copies the overlay out of the
# WSL build tree into %LOCALAPPDATA%\Programs\kartograf and puts a shortcut
# on the Windows desktop.
#
# A copy rather than a shortcut into the build tree: a \\wsl.localhost path
# only resolves while WSL is running, and the build tree holds a half-linked
# exe during every rebuild. Run again after a build to update the copy.
#
#   cmake --build --preset windows-dev && scripts/install.sh
set -euo pipefail

root="$(cd "$(dirname "$0")/.." && pwd)"
exe="$root/build/windows-dev/kartograf.exe"
[ -f "$exe" ] || { echo "no build at $exe; run cmake --build --preset windows-dev" >&2; exit 1; }

localappdata="$(wslpath "$(cmd.exe /c 'echo %LOCALAPPDATA%' 2>/dev/null | tr -d '\r')")"
dest="$localappdata/Programs/kartograf"
mkdir -p "$dest"

# Windows will not overwrite a running exe, but it will rename one. Move the
# old copy aside so an update works while the overlay is open; the running
# instance keeps going and the next start picks up the new file.
#
# Each aside copy gets its own name: the one moved aside last time may still
# be running -- installed twice without a restart -- and cannot be replaced.
# Copies no longer running are deleted on the way.
if [ -f "$dest/kartograf.exe" ]; then
    for stale in "$dest"/kartograf.old*.exe; do
        [ -e "$stale" ] && rm -f "$stale" 2>/dev/null || true
    done
    mv "$dest/kartograf.exe" "$dest/kartograf.old-$(date +%Y%m%d-%H%M%S).exe"
fi
cp "$exe" "$dest/kartograf.exe"
echo "installed $(wslpath -w "$dest/kartograf.exe")"

# The program was called tarkov-help before. Its install goes once no copy of
# it is running; one still running keeps its exe, and the next install retries.
# Its data folder is not touched here: Kartograf moves that itself on start.
legacy="$localappdata/Programs/tarkov-help"
if [ -d "$legacy" ]; then
    if tasklist.exe /FI "IMAGENAME eq tarkov_help.exe" /NH 2>/dev/null | grep -qi tarkov_help; then
        echo "tarkov-help is still running; close it, then start Kartograf" >&2
    else
        rm -rf "$legacy" && echo "removed the old install, $(wslpath -w "$legacy")"
    fi
fi

# The shortcut. WScript.Shell resolves the real desktop, OneDrive-redirected
# or not.
timeout 60 powershell.exe -NoProfile -Command "
  \$desktop = [Environment]::GetFolderPath('Desktop');
  \$link = (New-Object -ComObject WScript.Shell).CreateShortcut((Join-Path \$desktop 'Kartograf.lnk'));
  \$link.TargetPath = '$(wslpath -w "$dest/kartograf.exe")';
  \$link.WorkingDirectory = '$(wslpath -w "$dest")';
  \$link.Description = 'Escape from Tarkov overlay';
  \$link.Save();
  Remove-Item -ErrorAction SilentlyContinue (Join-Path \$desktop 'Tarkov Help.lnk');
  Write-Output ('shortcut ' + (Join-Path \$desktop 'Kartograf.lnk'))"
