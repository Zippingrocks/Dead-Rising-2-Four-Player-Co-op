param(
    [Parameter(Mandatory = $true)][int]$GamePid,
    [int]$X = 0, [int]$Y = 0, [int]$Width = 0, [int]$Height = 0
)

# Dead Rising 2 <-> Case Zero switch helper, started by the Case Zero runtime (dinput8.dll) when the CASE: ZERO or
# DEAD RISING 2 menu button is chosen. DR2 loads its data once at boot, so a switch closes the game and starts it again.
# The helper waits for the game to exit and relaunches through Steam only if the player confirmed the switch: the
# one-shot Launch key is still in case_zero_campaign.ini and the runtime marked LaunchConfirmed=1 on DR2's clean quit
# (answering NO removes the key; a crash or a killed process never sets the mark).
$ErrorActionPreference = "SilentlyContinue"
$iniPath = Join-Path (Split-Path -Parent $MyInvocation.MyCommand.Path) "case_zero_campaign.ini"

Wait-Process -Id $GamePid
$ini = Get-Content $iniPath -Raw
if ($ini -match '(?m)^Launch=\S' -and $ini -match '(?m)^LaunchConfirmed=1') {
    Start-Process "steam://rungameid/45740"
}
