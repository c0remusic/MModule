# Regenerates every vector of dt2_master_oracle.py into C:\dev\elektron-work\vectors-dt2, three processes at a time,
# each under a wall-clock guard. The first run of the init (0x1c15e3) writes init-0x1c15e3.snap there (about 70 s);
# run regen once with a single process first if that snapshot is missing, so that two processes do not both make it.
param([string]$Out = "C:\dev\elektron-work\vectors-dt2", [int]$Seconds = 1500)
$here = Split-Path -Parent $MyInvocation.MyCommand.Path
$script = Join-Path $here "dt2_master_oracle.py"
New-Item -ItemType Directory -Force $Out | Out-Null
if (-not (Test-Path (Join-Path $Out "init-0x1c15e3.snap"))) {
	pwsh -NoProfile -File (Join-Path $here "guard.ps1") -Seconds 300 -Log (Join-Path $Out "init.log") -Run "$script --out $Out --settle 1 --case sine_drive_000"
	if ($LASTEXITCODE -ne 0) { "init failed"; exit 1 }
}
$groups = @(
	"--case hit_thr_100 --case hit_thr_050 --case hit_thr_000",
	"--case sine_thr_100 --case sine_thr_050 --case sine_thr_000",
	"--case sine_drive_000 --case sine_drive_050 --case sine_drive_100 --case hit_thr_050_drive_050"
)
$jobs = @()
for ($i = 0; $i -lt $groups.Count; $i++) {
	$log = Join-Path $Out "run$i.log"
	$jobs += Start-Process pwsh -ArgumentList @("-NoProfile", "-File", (Join-Path $here "guard.ps1"), "-Seconds", $Seconds,
		"-Log", $log, "-Run", "`"$script --out $Out $($groups[$i])`"") -PassThru -NoNewWindow
}
$jobs | ForEach-Object { $_.WaitForExit() }
$failed = $jobs | Where-Object { $_.ExitCode -ne 0 }
Get-Content (Join-Path $Out "run*.log")
if ($failed) { "a group failed"; exit 1 }
