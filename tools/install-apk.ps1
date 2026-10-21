# Build the release APK and install it on an R1 over adb, keeping the app's settings.
#
#   .\tools\install-apk.ps1 -Ip 192.168.1.224             # build + install
#   .\tools\install-apk.ps1 -Ip 192.168.1.224 -SkipBuild  # install the last build
#
# Firmware 3448 drops any adb shell command that starts with "pm" ("error: closed"), so the package
# manager is called through app_process, which is what /system/bin/pm does itself.
param(
    [Parameter(Mandatory = $true)][string]$Ip,
    [switch]$SkipBuild
)
$root = Split-Path $PSScriptRoot -Parent
$apk = Join-Path $root "app\build\outputs\apk\release\app-release.apk"
$adb = Join-Path $env:LOCALAPPDATA "Android\Sdk\platform-tools\adb.exe"
if (-not (Test-Path $adb)) { $adb = "adb" }
$serial = "${Ip}:5555"
$pm = "export CLASSPATH=/system/framework/pm.jar; app_process /system/bin com.android.commands.pm.Pm"

if (-not $SkipBuild) {
    if (-not $env:JAVA_HOME) {
        $jdk = Get-ChildItem "C:\Program Files\Microsoft" -Directory -Filter "jdk-17*" | Select-Object -First 1
        if ($jdk) { $env:JAVA_HOME = $jdk.FullName }
    }
    Push-Location $root
    try {
        & .\gradlew.bat --no-daemon :app:assembleRelease
        if ($LASTEXITCODE -ne 0) { throw "build failed" }
    } finally { Pop-Location }
}
if (-not (Test-Path $apk)) { throw "no APK at $apk" }

& $adb connect $serial | Out-Host
& $adb -s $serial push $apk /data/local/tmp/rc.apk
if ($LASTEXITCODE -ne 0) { throw "push failed" }
$out = & $adb -s $serial shell "$pm install -r /data/local/tmp/rc.apk"
$out | Out-Host
& $adb -s $serial shell "rm -f /data/local/tmp/rc.apk; rm -f /sdcard/control.html"
if (-not ($out -match "Success")) { throw "install failed" }
& $adb -s $serial shell "dumpsys package vn.dbrobot.r1 | grep -E 'versionName|lastUpdateTime'; am start -n vn.dbrobot.r1/info.dourok.voicebot.MainActivity" | Out-Host
