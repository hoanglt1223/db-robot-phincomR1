# Set the R1's system language without root, e.g. Vietnamese:
#
#   .\tools\locale\set-locale.ps1 -Ip 192.168.1.224 -Language vi -Country VN
#
# persist.sys.language needs root, but the shell user holds CHANGE_CONFIGURATION, and
# system_server saves the locale itself when a configuration change says the user picked it.
param(
    [Parameter(Mandatory = $true)][string]$Ip,
    [string]$Language = "vi",
    [string]$Country = "VN"
)
$sdk = Join-Path $env:LOCALAPPDATA "Android\Sdk"
$adb = Join-Path $sdk "platform-tools\adb.exe"
$d8 = Get-ChildItem (Join-Path $sdk "build-tools") -Recurse -Filter d8.bat | Select-Object -First 1
if (-not $env:JAVA_HOME) {
    $env:JAVA_HOME = (Get-ChildItem "C:\Program Files\Microsoft" -Directory -Filter "jdk-17*" | Select-Object -First 1).FullName
}
$out = Join-Path $env:TEMP "r1-setlocale"
New-Item -ItemType Directory -Force $out | Out-Null

& "$env:JAVA_HOME\bin\javac.exe" --release 8 -d $out (Join-Path $PSScriptRoot "SetLocale.java")
& $d8.FullName --min-api 22 --output (Join-Path $out "setlocale.jar") (Join-Path $out "SetLocale.class")
$serial = "${Ip}:5555"
& $adb connect $serial | Out-Host
& $adb -s $serial push (Join-Path $out "setlocale.jar") /data/local/tmp/setlocale.jar
& $adb -s $serial shell "export CLASSPATH=/data/local/tmp/setlocale.jar; app_process /system/bin SetLocale $Language $Country; rm /data/local/tmp/setlocale.jar; getprop persist.sys.language; getprop persist.sys.country"
