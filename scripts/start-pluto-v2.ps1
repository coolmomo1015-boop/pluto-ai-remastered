[CmdletBinding()]
param(
  [string]$StarCraft = '',
  [string]$Pluto = '',
  [ValidateRange(-1,1000)][int]$SpeedMs = 0,
  [switch]$DrawGraph,
  [switch]$Multiplayer,
  [switch]$Challenge,
  [switch]$VerifyOnly,
  [switch]$PackageCheck
)

$ErrorActionPreference = 'Stop'

# Resolve paths only after the script has started. $PSScriptRoot is not reliable
# in a param() default expression, which caused Split-Path to receive an empty
# string when launched from the packaged .cmd files.
$scriptPath = $PSCommandPath
if ([string]::IsNullOrWhiteSpace($scriptPath)) {
  $scriptPath = $MyInvocation.MyCommand.Path
}
if ([string]::IsNullOrWhiteSpace($scriptPath)) {
  throw 'Cannot determine the path of start-pluto-v2.ps1.'
}
$scriptDirectory = [System.IO.Path]::GetDirectoryName([System.IO.Path]::GetFullPath($scriptPath))
$projectRoot = Split-Path -Path $scriptDirectory -Parent

if ([string]::IsNullOrWhiteSpace($StarCraft)) {
  $programFilesX86 = [Environment]::GetEnvironmentVariable('ProgramFiles(x86)')
  if ([string]::IsNullOrWhiteSpace($programFilesX86)) {
    throw 'Cannot locate Program Files (x86). Pass -StarCraft with the full StarCraft.exe path.'
  }
  $StarCraft = Join-Path $programFilesX86 'StarCraft\x86\StarCraft.exe'
}
if ([string]::IsNullOrWhiteSpace($Pluto)) {
  $Pluto = Join-Path $projectRoot 'model-v2\pluto.dll'
}

$bin = Join-Path $projectRoot 'bin'
$runtime = Join-Path $projectRoot $(if ($Challenge) { 'runtime-v2-challenge' } elseif ($Multiplayer) { 'runtime-v2-multiplayer' } else { 'runtime-v2' })
if ($PackageCheck) {
  $modelDirectory = Join-Path (Split-Path -Path $Pluto -Parent) 'pluto'
  $requiredPaths = @(
    $Pluto,
    (Join-Path $modelDirectory 'pluto_infer.exe'),
    (Join-Path $modelDirectory 'pluto_weights.bin'),
    (Join-Path $bin 'pluto-scr.dll'),
    (Join-Path $bin 'scr-loader.exe')
  )
  foreach ($requiredPath in $requiredPaths) {
    if (-not (Test-Path -LiteralPath $requiredPath -PathType Leaf)) {
      throw "Package is missing required file: $requiredPath"
    }
  }
  Write-Output "Package paths resolved successfully from: $projectRoot"
  Write-Output "Pluto model: $Pluto"
  Write-Output "Bridge binaries: $bin"
  return
}

function Assert-Hash([string]$Path, [string]$Expected) {
  if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) { throw "Missing file: $Path" }
  # Use the built-in .NET implementation, including on Windows PowerShell 5.1
  # installations where the Get-FileHash function is not automatically loaded.
  $algorithm = [System.Security.Cryptography.SHA256]::Create()
  $stream = $null
  try {
    $stream = [System.IO.File]::OpenRead($Path)
    $actual = [System.BitConverter]::ToString($algorithm.ComputeHash($stream)).Replace('-', '').ToLowerInvariant()
  } finally {
    if ($null -ne $stream) { $stream.Dispose() }
    $algorithm.Dispose()
  }
  if ($actual -ne $Expected) {
    throw "Unsupported or damaged file: $Path. See README.md for supported versions."
  }
}
$StarCraft = (Resolve-Path -LiteralPath $StarCraft).Path
$Pluto = (Resolve-Path -LiteralPath $Pluto).Path
Assert-Hash $StarCraft '32dbbdd001dd381cb1b3a719b7ad1fc918a9d4bc99661c675e00254efecca827'
Assert-Hash $Pluto '603fea2f77a4b652f29b832c2977cf5bc9d83bbcb927febd04499220020ce3bc'
$modelDirectory = Join-Path (Split-Path $Pluto -Parent) 'pluto'
Assert-Hash (Join-Path $modelDirectory 'pluto_infer.exe') '3f2814a51034b9817b396e52b38d30341ede506a2baedc7cbc11cb6ebcece2cc'
Assert-Hash (Join-Path $modelDirectory 'pluto_weights.bin') 'c53a559b4910777dc0e1c561c0d89d243091f465d8e9fc0f5f72e56597067d86'
foreach ($name in @('pluto-scr.dll','scr-loader.exe')) {
  if (-not (Test-Path -LiteralPath (Join-Path $bin $name))) { throw "Missing bin/$name. Run scripts/build.ps1 first." }
}
if ($VerifyOnly) { Write-Output 'Verified StarCraft, Pluto DLL, inference engine, weights and bridge files.'; return }
if (Get-Process -Name StarCraft -ErrorAction SilentlyContinue) {
  throw 'Exit the existing StarCraft process before starting Pluto. No existing game was changed.'
}
New-Item -ItemType Directory -Path $runtime -Force | Out-Null
$previousLogs = @('bridge.log','pluto.log','pluto_infer.log') | ForEach-Object { Join-Path $runtime $_ } | Where-Object { Test-Path -LiteralPath $_ }
if ($previousLogs) {
  $archive = Join-Path $runtime ('logs/' + (Get-Date -Format 'yyyyMMdd-HHmmss-fff'))
  New-Item -ItemType Directory -Path $archive -Force | Out-Null
  foreach ($path in $previousLogs) { Move-Item -LiteralPath $path -Destination $archive }
}
Copy-Item -LiteralPath (Join-Path $bin 'pluto-scr.dll') -Destination $runtime -Force
@('[pluto]',"module=$Pluto","speed_ms=$SpeedMs","multiplayer=$([int]$Multiplayer.IsPresent)","challenge=$([int]$Challenge.IsPresent)",'apm_limit=400') | Set-Content -LiteralPath (Join-Path $runtime 'bridge.ini') -Encoding Unicode
$previousDraw = $env:BWRL_DRAW
$previousStraddle = $env:BWRL_STRADDLE
$previousBudget = $env:BWRL_FRAME_BUDGET_MS
$startedGame = $null
$game = $null
try {
  if ($DrawGraph) { $env:BWRL_DRAW = '1' }
  if ($Multiplayer) {
    $env:BWRL_STRADDLE = '1'
    if (-not $env:BWRL_FRAME_BUDGET_MS) { $env:BWRL_FRAME_BUDGET_MS = '10' }
  }
  # Do not hide StarCraft: a hidden launch leaves MainWindowHandle=0 and
  # prevents the bridge loader from attaching to the interactive game.
  $startedGame = Start-Process -FilePath $StarCraft -ArgumentList '-launch' -WorkingDirectory $runtime -PassThru
} finally {
  $env:BWRL_DRAW = $previousDraw
  $env:BWRL_STRADDLE = $previousStraddle
  $env:BWRL_FRAME_BUDGET_MS = $previousBudget
}
try {
  $ready = $false
  $gameProcessName = [System.IO.Path]::GetFileNameWithoutExtension($StarCraft)
  for ($attempt=0; $attempt -lt 90; $attempt++) {
    # Battle.net may hand off from the short-lived launch process to another
    # StarCraft.exe PID. Find the actual visible game process, not just the
    # first PID returned by Start-Process.
    $candidates = @(Get-Process -Name $gameProcessName -ErrorAction SilentlyContinue)
    foreach ($candidate in $candidates) {
      try {
        $candidate.Refresh()
        $candidatePath = $candidate.Path
      } catch {
        $candidatePath = ''
      }
      if ($candidatePath -and
          [System.IO.Path]::GetFullPath($candidatePath) -ine [System.IO.Path]::GetFullPath($StarCraft)) {
        continue
      }
      if ($candidate.MainWindowHandle -ne 0) {
        $game = $candidate
        $ready = $true
        break
      }
    }
    if ($ready) { break }
    Start-Sleep -Milliseconds 500
  }
  if (-not $ready) {
    $observed = @(Get-Process -Name $gameProcessName -ErrorAction SilentlyContinue |
      ForEach-Object {
        try { $_.Refresh(); $path = $_.Path } catch { $path = '<path unavailable>' }
        "PID=$($_.Id), MainWindowHandle=$($_.MainWindowHandle), Path=$path"
      })
    $details = if ($observed.Count) { $observed -join '; ' } else { 'No StarCraft process was found.' }
    $startedState = if ($startedGame.HasExited) { "Initial PID $($startedGame.Id) exited with code $($startedGame.ExitCode)." } else { "Initial PID $($startedGame.Id) is still running." }
    throw "StarCraft did not create a visible game window within 45 seconds. $startedState $details"
  }
  & (Join-Path $bin 'scr-loader.exe') $game.Id (Join-Path $runtime 'pluto-scr.dll')
  if ($LASTEXITCODE -ne 0) { throw 'Bridge loading failed.' }
  $log = Join-Path $runtime 'bridge.log'
  $initialized = $false
  for ($attempt=0; $attempt -lt 30; $attempt++) {
    if ((Test-Path -LiteralPath $log) -and (Select-String -LiteralPath $log -SimpleMatch '"result":"MH_OK"' -Quiet)) { $initialized=$true; break }
    Start-Sleep -Milliseconds 200
  }
  if (-not $initialized) { throw "Bridge initialization failed. Read $log" }
  if ($Challenge) {
    Write-Output "Experimental Pluto v2.0 1-v-many bridge ready in StarCraft process $($game.Id). Choose Melee, Free For All or Top vs Bottom."
    Write-Output 'Put Pluto alone on its side, with 1-7 opponents. This uses the original 1v1 model; match quality is unverified.'
  } elseif ($Multiplayer) {
    Write-Output "Experimental Pluto v2.0 multiplayer bridge ready in StarCraft process $($game.Id). Choose a custom 1v1 Melee game."
    Write-Output 'Network synchronization with a second client still requires validation. The game controls multiplayer speed.'
  } else {
    Write-Output "Pluto v2.0 ready in StarCraft process $($game.Id). Choose Single Player > Play Custom, Melee, one Computer opponent."
  }
  Write-Output "Logs and persistent opening statistics: $runtime"
  Write-Output "Transparent command limiter: 400 packets/minute (approximate APM). This is a gameplay limit, not an anti-cheat bypass."
} catch {
  # The initial launch and any Battle.net handoff process belong to this launch,
  # because the script rejects pre-existing StarCraft instances above.
  if ($null -ne $game) {
    try { $game.Refresh(); if (-not $game.HasExited) { Stop-Process -Id $game.Id -Force } } catch {}
  } elseif ($null -ne $startedGame) {
    try { $startedGame.Refresh(); if (-not $startedGame.HasExited) { Stop-Process -Id $startedGame.Id -Force } } catch {}
  }
  throw
}
