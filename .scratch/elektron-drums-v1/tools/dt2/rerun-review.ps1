# Reruns the cases the 2026-10-26 review changed (the threshold cases, which now report the ratio entry read, and the
# combined case), three processes, each under the wall-clock guard. The drive cases are not rerun.
$here = Split-Path -Parent $MyInvocation.MyCommand.Path
$Out = "C:\dev\elektron-work\vectors-dt2"
$script = Join-Path $here "dt2_master_oracle.py"
$groups = @(
	"--case hit_thr_100 --case hit_thr_050",
	"--case hit_thr_000 --case hit_thr_050_drive_050",
	"--case sine_thr_100 --case sine_thr_050 --case sine_thr_000"
)
$jobs = @()
for ($i = 0; $i -lt $groups.Count; $i++) {
	$log = Join-Path $Out "review$i.log"
	$jobs += Start-Process pwsh -ArgumentList @("-NoProfile", "-File", (Join-Path $here "guard.ps1"), "-Seconds", 1200,
		"-Log", $log, "-Run", "`"$script --out $Out $($groups[$i])`"") -PassThru -NoNewWindow
}
$jobs | ForEach-Object { $_.WaitForExit() }
Get-Content (Join-Path $Out "review*.log")
Get-Content (Join-Path $Out "review*.log.err") | Select-Object -Last 10
if ($jobs | Where-Object { $_.ExitCode -ne 0 }) { "a group failed"; exit 1 }
