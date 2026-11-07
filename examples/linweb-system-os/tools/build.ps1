<#
Builds the OS module on Windows (the folder build-win/) and prints only what matters. The engine it builds against is the copy in ..\linweb-engine.

    powershell -File tools\build.ps1                            builds linweb-system-os
    powershell -File tools\build.ps1 -Target check_gamepad      compile-checks one piece (see the check_* targets of CMakeLists.txt)

Several agents can work here at once and the compilers lock the files they use, so a build takes a lock first: the folder ..\.build-lock-module is created
(atomic), the build runs, the folder is removed. Another build waits.
#>
param(
    [string]$Target = 'linweb-system-os',
    [string]$BuildDir = 'build-win',
    [int]$WaitSeconds = 900
)

$root = Split-Path -Parent $PSScriptRoot
$lock = Join-Path (Split-Path -Parent $root) '.build-lock-module'
$deadline = (Get-Date).AddSeconds($WaitSeconds)
while ($true) {
    try { New-Item -ItemType Directory -Path $lock -ErrorAction Stop | Out-Null; break } catch {
        if ((Get-Date) -gt $deadline) { Write-Output "BUILD LOCK BUSY for $WaitSeconds s: $lock"; exit 2 }
        Start-Sleep -Seconds 5
    }
}
try {
    Set-Location $root
    if (-not (Test-Path (Join-Path $root $BuildDir))) {
        cmake -S . -B $BuildDir "-Dglfw3_DIR=C:/Program Files (x86)/GLFW/lib/cmake/glfw3" | Out-Null
    }
    $out = cmake --build $BuildDir --config Release --target $Target --parallel 8 2>&1
    $problems = $out | Select-String -Pattern ' error |fatal error|: error C|undefined reference|LNK[0-9]+' | ForEach-Object { $_.Line.Trim() }
    if ($problems) { $problems | Select-Object -First 40; Write-Output "BUILD FAILED ($Target)"; exit 1 }
    Write-Output "BUILD OK ($Target)"
} finally {
    Remove-Item -Recurse -Force $lock -ErrorAction SilentlyContinue
}
