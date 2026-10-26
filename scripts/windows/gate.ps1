# MoDule's gate (CLAUDE.md, "Gate"): the build of the gate's targets, then ctest on them, with what a reader needs when it
# goes red. Run it from anywhere:
#   powershell -ExecutionPolicy Bypass -File scripts\windows\gate.ps1 [-Log <file>]
# Green: "GATE GREEN" on the last line and exit 0. Red: "GATE RED", exit 1, each failing test's own output in the log.
#
# - The display is kept awake while it runs: mdDrumsVst3HostTest captures the editor's window, which draws nothing on a
#   display that went to sleep.
# - The machine's load is measured before the tests and written at the top of the log: the tests that time the audio
#   thread against a wall clock (TimingTests below) fail when other programs take the processor (Ableton Live playing,
#   another session's build or survey). Such a test that fails is run once more: green on the second run, the gate says
#   so and names the load ("timing test failed under load, passed on its second run"); red twice, the gate is red.
# - Every other test's failure is red at once.
# - A test that skips (exit 77: its firmware, OS or vectors not found) is red too: ctest counts it as passed, and a
#   skipped test proves nothing. The log names each one, and the Syntakt variables the gate could not set.
param(
    [string]$Log = (Join-Path ([System.IO.Path]::GetTempPath()) "module-gate.log"),
    [string]$Build = "temp\cmake_vs22",
    [string]$SyntaktData = "C:\dev\elektron-work",
    [string]$SyntaktOs = "$env:USERPROFILE\Downloads\Syntakt OS 1.40A\Syntakt OS\Syntakt_OS1.41.syx"
)
$ErrorActionPreference = 'Continue'
$root = Resolve-Path "$PSScriptRoot\..\.."
Set-Location $root

$targets = @(
    "mdAutomationMidiTest", "mdKitDumpFirmwareTest", "mdTrigLatencyFirmwareTest", "mdEngineFirmwareTest",
    "mdDrumsEngineTest", "mdDrumsDeviceTest", "mdDrumsProcessorTest", "mdDrumsSkinTest", "mdDrumsScreensTest",
    "mdDrumsBankTest", "mdDrumsHitPreviewTest", "mdDrumsFirstHitTest", "mdDrumsVst3HostTest", "mdDrumsUpdateTest",
    "mdX20OsTest", "mdX20BuiltinsTest", "mdX20PortTest", "synthLibResamplerTimingTest", "synthLibOutputChannelsTest",
    "mdProtocolKitTest", "stEngineOracleTest", "stVoiceCpuTest", "stMainOracleTest", "stSyntaktTest", "stSyntakt16Test",
    "stSimdTest", "stDrumsFirmwareTest", "stDrumsVst3HostTest", "mdVoicePortTest", "stFastBdModernTest")
# The Syntakt's two oracle tests run a second time with the AVX2 kernels off (SYNTAKT_NO_AVX2, set by ctest)
$tests = $targets + @("mdSlideFirmwareTest", "stEngineOracleTestScalar", "stMainOracleTestScalar") |
    Where-Object { $_ -ne "mdTrigLatencyFirmwareTest" }
# Tests that compare the audio thread's wall-clock time with a bound
$timingTests = @("mdDrumsVst3HostTest", "mdDrumsEngineTest", "stDrumsVst3HostTest")

$rom = "$env:LOCALAPPDATA\Programs\Gearmulator-Elektron"
$env:GEARMULATOR_MD_FIRMWARE_BIN = "$rom\elektron_sps1-1uw_os1.63.bin"
$env:GEARMULATOR_MM_FIRMWARE_BIN = "$rom\elektron_sfx6-60_os1.32b.bin"
$env:MD_X20_OS = "$rom\Machinedrum_SPS1-UW_OS_X.20_B.syx"
$env:MD_AUTOMATION_REQUIRE_FIRMWARE = "1"

# The Syntakt's tests read the user's OS 1.41 and the original routines' vectors and references, firmware-derived data
# kept outside the repository (CLAUDE.md, "Gate"): each variable left unset is set from $SyntaktData (the .syx from
# $SyntaktOs) where its files are
$joinFiles = { param($_pattern) (Get-ChildItem $_pattern -ErrorAction SilentlyContinue | ForEach-Object FullName) -join ";" }
$syntakt = [ordered]@{
    SYNTAKT_OS_SYX       = $SyntaktOs
    SYNTAKT_SECTION3     = "$SyntaktData\syntakt-1.41\section_3_MAIN_OS.bin"
    SYNTAKT_SECTION7     = "$SyntaktData\syntakt-1.41\section_7_BLOB.bin"
    SYNTAKT_VECTORS      = & $joinFiles "$SyntaktData\vectors\*.stv"
    SYNTAKT_MAIN_VECTORS = & $joinFiles "$SyntaktData\vectors-main\*.stv"
    SYNTAKT_MAIN_OUTPUTS = "$SyntaktData\vectors-main\block_sequence.sto;$SyntaktData\vectors-main\mix_sequence.sto"
    SYNTAKT_SCENARIO     = "$SyntaktData\vectors-main\syntakt_scenario.sto"
    SYNTAKT_REFERENCES   = "$SyntaktData\references"
    SYNTAKT_SAMPLES      = "$SyntaktData\references\twinshot_samples.bin"
}
$syntaktMissing = @()
foreach($name in $syntakt.Keys)
{
    if([Environment]::GetEnvironmentVariable($name)) { continue }
    $value = $syntakt[$name]
    if($value -and -not ($value -split ";" | Where-Object { -not (Test-Path $_) })) { Set-Item "env:$name" $value }
    else { $syntaktMissing += $name }
}

function Say([string]$_text) { $_text | Tee-Object -FilePath $Log -Append }

# What else uses the processor: other processes' CPU over three seconds, in cores
function Measure-Load
{
    $self = $PID
    $first = @{}
    Get-Process | ForEach-Object { if($_.CPU) { $first[$_.Id] = $_.CPU } }
    Start-Sleep -Seconds 3
    $busy = Get-Process | Where-Object { $_.Id -ne $self -and $first.ContainsKey($_.Id) -and $_.CPU } | ForEach-Object {
        [pscustomobject]@{ Name = $_.Name; Cores = ($_.CPU - $first[$_.Id]) / 3 }
    } | Group-Object Name | ForEach-Object {
        [pscustomobject]@{ Name = $_.Name; Cores = ($_.Group | Measure-Object Cores -Sum).Sum }
    } | Sort-Object Cores -Descending
    $total = ($busy | Measure-Object Cores -Sum).Sum
    $top = ($busy | Select-Object -First 5 | ForEach-Object { "{0} {1:N1}" -f $_.Name, $_.Cores }) -join ", "
    return [pscustomobject]@{ Total = $total; Top = $top }
}

# The display kept on (a display already asleep stays so under these flags: a zero mouse move wakes it first)
Add-Type @"
using System;
using System.Runtime.InteropServices;
public static class GateAwake
{
    [DllImport("kernel32.dll")] public static extern uint SetThreadExecutionState(uint esFlags);
    [DllImport("user32.dll")] public static extern void mouse_event(uint dwFlags, int dx, int dy, uint dwData, UIntPtr dwExtraInfo);
}
"@
[GateAwake]::mouse_event(1, 0, 0, 0, [UIntPtr]::Zero)
$continuous = [uint32]2147483648
[GateAwake]::SetThreadExecutionState($continuous -bor [uint32]3) | Out-Null

$green = $false
try
{
    Set-Content -Path $Log -Value ("MoDule gate {0}, {1}" -f (Get-Date -Format "yyyy-MM-dd HH:mm:ss"), (git rev-parse --short HEAD))
    if($syntaktMissing.Count -gt 0) { Say "Syntakt data not found (-SyntaktData, -SyntaktOs): $($syntaktMissing -join ', ')" }
    $start = Get-Date
    $built = cmake --build $Build --config Release -j 6 --target $targets 2>&1
    $buildExit = $LASTEXITCODE
    $built | Select-String -Pattern "error C|error LNK|: error|error MSB" | Select-Object -First 20 | ForEach-Object { Say $_.Line }
    Say "build exit $buildExit"
    if($buildExit -ne 0) { throw "the build failed" }

    Copy-Item "$rom\*.bin" "$Build\source\elektron\md\mdJucePlugin\Release" -ErrorAction SilentlyContinue
    $load = Measure-Load
    Say ("machine load before the tests: {0:N1} cores besides the gate ({1})" -f $load.Total, $load.Top)
    if($load.Total -gt 1.5)
    {
        Say "WARNING: a busy machine; the timing tests ($($timingTests -join ', ')) may fail on its account"
    }

    Push-Location $Build
    $regex = "^(" + ($tests -join "|") + ")$"
    $out = ctest -C Release -R $regex --output-on-failure 2>&1
    $ctestExit = $LASTEXITCODE
    $out | Out-File -FilePath "$Log.ctest.txt" -Encoding utf8
    $out | Select-String -Pattern "Test +#|tests passed|tests failed|Total Test time" | ForEach-Object { Say $_.Line }
    $failed = @($out | Select-String -Pattern "^\s*\d+ - (\S+) \((?!Skipped)" | ForEach-Object { $_.Matches[0].Groups[1].Value })
    # The failing tests' own lines, the ones a reader needs (the whole output is in <log>.ctest.txt)
    if($failed.Count -gt 0)
    {
        Say "---- failing: $($failed -join ', ')"
        $out | Select-String -Pattern "FAIL|stalled|drew nothing|exception|A block of 128" |
            Where-Object { $_.Line -notmatch "tests failed|Test +#" } |
            Select-Object -First 20 | ForEach-Object { Say ("  " + $_.Line.Trim()) }
    }
    Say "ctest exit $ctestExit"
    $skipped = @($out | Select-String -Pattern "Test +#\d+: (\S+) .*\*\*\*Skipped" | ForEach-Object { $_.Matches[0].Groups[1].Value })
    if($skipped.Count -gt 0) { Say "---- skipped, which the gate takes as failed: $($skipped -join ', ')" }

    $red = @($failed | Where-Object { $timingTests -notcontains $_ }) + $skipped
    $retry = @($failed | Where-Object { $timingTests -contains $_ })
    if($ctestExit -eq 0 -and $skipped.Count -eq 0)
    {
        $green = $true
    }
    elseif($red.Count -eq 0 -and $retry.Count -gt 0)
    {
        # Only timing tests failed: once more, with the load measured again
        $load2 = Measure-Load
        Say ("second run of {0}, machine load {1:N1} cores ({2})" -f ($retry -join ", "), $load2.Total, $load2.Top)
        $again = ctest -C Release -R ("^(" + ($retry -join "|") + ")$") --output-on-failure 2>&1
        $againExit = $LASTEXITCODE
        $again | Out-File -FilePath "$Log.ctest-rerun.txt" -Encoding utf8
        $again | Select-String -Pattern "Test +#|tests passed|stalled|FAIL" | ForEach-Object { Say $_.Line }
        if($againExit -eq 0)
        {
            Say "timing test failed under load, passed on its second run: $($retry -join ', ')"
            $green = $true
        }
    }
    Pop-Location
    Say ("elapsed {0:N0} s" -f ((Get-Date) - $start).TotalSeconds)
}
catch
{
    Say "stopped: $_"
}
finally
{
    [GateAwake]::SetThreadExecutionState($continuous) | Out-Null
    Say $(if($green) { "GATE GREEN" } else { "GATE RED" })
}
exit $(if($green) { 0 } else { 1 })
