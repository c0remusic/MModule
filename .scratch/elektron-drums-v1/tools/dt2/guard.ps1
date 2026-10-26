# Runs digikit's Python on a script under a wall-clock guard: killed on timeout. Usage:
#   pwsh -NoProfile -File guard.ps1 -Seconds 600 -Log out.txt -Run 'script.py --opt value'
param(
	[int]$Seconds = 600,
	[Parameter(Mandatory)][string]$Log,
	[Parameter(Mandatory)][string]$Run
)
$py = if ($env:DIGIKIT) { Join-Path $env:DIGIKIT ".venv\Scripts\python.exe" } else { "C:\dev\elektron-work\scratch-arch\ext\digikit\.venv\Scripts\python.exe" }
$p = Start-Process -FilePath $py -ArgumentList $Run -PassThru -NoNewWindow -RedirectStandardOutput $Log -RedirectStandardError "$Log.err"
if (-not $p.WaitForExit($Seconds * 1000)) { $p.Kill(); "TIMEOUT after $Seconds s" | Add-Content $Log; exit 124 }
exit $p.ExitCode
