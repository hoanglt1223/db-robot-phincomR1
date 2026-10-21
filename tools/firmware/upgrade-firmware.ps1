# One step of the Phicomm firmware upgrade chain (see docs/firmware-upgrade.md). Run it, let the
# speaker reboot and install the patch, then run it again until it reports 3448.
#
#   .\tools\firmware\upgrade-firmware.ps1 -Ip 192.168.1.224 -PackageDir D:\path\to\firmware
#
# -PackageDir holds the incremental-ota-<build>.zip files listed in manifest.txt.
param(
    [Parameter(Mandatory = $true)][string]$Ip,
    [Parameter(Mandatory = $true)][string]$PackageDir
)
$adb = Join-Path $env:LOCALAPPDATA "Android\Sdk\platform-tools\adb.exe"
if (-not (Test-Path $adb)) { $adb = "adb" }
$serial = "${Ip}:5555"
$pm = "export CLASSPATH=/system/framework/pm.jar; app_process /system/bin com.android.commands.pm.Pm"

& $adb connect $serial | Out-Host
$ver = (& $adb -s $serial shell getprop ro.build.version.incremental).Trim()
if ($ver -eq "3448") { Write-Host "Firmware is 3448, the last Phicomm build. Nothing to do."; exit 0 }

$prop = Join-Path $PSScriptRoot "ota\ota-$ver.txt"
$zip = Join-Path $PackageDir "incremental-ota-$ver.zip"
if (-not (Test-Path $prop)) { throw "no upgrade path from build $ver" }
if (-not (Test-Path $zip)) { throw "missing $zip" }
$want = ((Get-Content $prop | Select-String "^ota_debug_md5=") -replace "^ota_debug_md5=", "").ToString().Trim()
$have = (Get-FileHash $zip -Algorithm MD5).Hash.ToLower()
if ($want -ne $have) { throw "$zip md5 $have, expected $want" }
$next = ((Get-Content $prop | Select-String "^ota_cur_fw_ver=") -replace "^ota_cur_fw_ver=1\.0\.0\.", "").ToString().Trim()

Write-Host "Upgrading $ver -> $next"
# The update service only reads otaprop.txt it has not seen yet; clearing its data makes it look again.
& $adb -s $serial shell "rm -f /sdcard/otaprop.txt /sdcard/incremental-ota-*.zip; $pm unhide com.phicomm.speaker.otaservice; $pm clear com.phicomm.speaker.otaservice" | Out-Host
& $adb -s $serial push $zip "/sdcard/incremental-ota-$ver.zip"
if ($LASTEXITCODE -ne 0) { throw "push failed" }
& $adb -s $serial push $prop /sdcard/otaprop.txt
if ($LASTEXITCODE -ne 0) { throw "push failed" }
& $adb -s $serial reboot
Write-Host "Rebooting. The patch installs once the speaker is back on the internet; the 3331 -> 3448 step forgets the saved Wi-Fi."
