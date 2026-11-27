[CmdletBinding()]
param(
    [ValidateSet('Editor','Package','Validate','Tests')][string]$Action = 'Editor',
    [string]$EngineRoot = '',
    [ValidateSet('Development','Shipping')][string]$Configuration = 'Development'
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$projectRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$projectFile = Join-Path $projectRoot 'BackyardMonstersNXT.uproject'
$logRoot = Join-Path $projectRoot 'BuildLogs'
New-Item -ItemType Directory -Path $logRoot -Force | Out-Null
$logFile = Join-Path $logRoot ('{0}-{1:yyyyMMdd-HHmmss}.log' -f $Action,(Get-Date))
Start-Transcript -Path $logFile -Force | Out-Null
try {
    if (-not $EngineRoot) {
        $candidates = @()
        foreach ($key in @('HKLM:\SOFTWARE\EpicGames\Unreal Engine\5.8','HKLM:\SOFTWARE\WOW6432Node\EpicGames\Unreal Engine\5.8')) {
            if (Test-Path $key) {
                $entry = Get-ItemProperty -LiteralPath $key
                if ($entry.PSObject.Properties.Name -contains 'InstalledDirectory') { $candidates += $entry.InstalledDirectory }
            }
        }
        if ($env:UE_58_ROOT) { $candidates += $env:UE_58_ROOT }
        $candidates += (Join-Path $env:ProgramFiles 'Epic Games\UE_5.8')
        $candidates += (Join-Path $env:USERPROFILE 'Epic Games\UE_5.8')
        foreach ($candidate in $candidates) {
            if ($candidate -and (Test-Path (Join-Path $candidate 'Engine\Build\BatchFiles\Build.bat'))) { $EngineRoot = $candidate; break }
        }
    }
    if (-not $EngineRoot) {
        throw 'Unreal Engine 5.8 was not found. Install it through Epic Games Launcher, or set UE_58_ROOT to its installation folder and run again.'
    }
    $EngineRoot = [IO.Path]::GetFullPath($EngineRoot)
    $versionFile = Join-Path $EngineRoot 'Engine\Build\Build.version'
    if (-not (Test-Path $versionFile)) { throw 'This folder does not contain an Unreal Engine installation.' }
    $version = Get-Content -LiteralPath $versionFile -Raw | ConvertFrom-Json
    if ($version.MajorVersion -ne 5 -or $version.MinorVersion -ne 8) { throw 'This project targets Unreal Engine 5.8. Point UE_58_ROOT at UE 5.8.' }
    $build = Join-Path $EngineRoot 'Engine\Build\BatchFiles\Build.bat'
    $editorCmd = Join-Path $EngineRoot 'Engine\Binaries\Win64\UnrealEditor-Cmd.exe'
    $editor = Join-Path $EngineRoot 'Engine\Binaries\Win64\UnrealEditor.exe'
    $uat = Join-Path $EngineRoot 'Engine\Build\BatchFiles\RunUAT.bat'
    foreach ($required in @($build,$editorCmd,$editor,$uat,$projectFile)) { if (-not (Test-Path -LiteralPath $required)) { throw "Missing required file: $required" } }
    Write-Host 'Building BackyardMonstersNXTEditor. Visual Studio C++ and the Windows SDK are required.' -ForegroundColor Cyan
    & $build 'BackyardMonstersNXTEditor' 'Win64' 'Development' "-Project=$projectFile" '-WaitMutex' '-NoHotReloadFromIDE'
    if ($LASTEXITCODE -ne 0) { throw "Unreal C++ compilation failed (exit $LASTEXITCODE). See this log; asset setup was not attempted." }

    if ($Action -eq 'Validate') {
        & $editorCmd $projectFile '-run=BYMSetup' '-ValidateOnly' '-unattended' '-nop4' '-NullRHI' '-nosound' '-stdout' '-FullStdOutLogOutput'
        if ($LASTEXITCODE -ne 0) { throw "Setup validation failed (exit $LASTEXITCODE)." }
    } else {
        Write-Host 'Importing the bundled original assets and preparing both maps. The first run can take several minutes.' -ForegroundColor Cyan
        & $editorCmd $projectFile '-run=BYMSetup' '-unattended' '-nop4' '-NullRHI' '-nosound' '-stdout' '-FullStdOutLogOutput'
        if ($LASTEXITCODE -ne 0) { throw "Automatic asset/map setup failed (exit $LASTEXITCODE). Packaging was not started." }
        if ($Action -eq 'Package') {
            $archive = Join-Path $projectRoot 'Packaged'
            & $uat 'BuildCookRun' "-project=$projectFile" '-noP4' '-platform=Win64' "-clientconfig=$Configuration" '-build' '-cook' '-stage' '-pak' '-iostore' '-archive' "-archivedirectory=$archive" '-utf8output' '-unattended'
            if ($LASTEXITCODE -ne 0) { throw "Windows packaging failed (exit $LASTEXITCODE)." }
            Write-Host "Packaged game: $archive" -ForegroundColor Green
            Write-Host 'Give everyone the entire Windows output folder. The same game can Host & Play or Join.'
        } elseif ($Action -eq 'Tests') {
            $reports = Join-Path $projectRoot ('TestResults\{0:yyyyMMdd-HHmmss}-{1}' -f (Get-Date),([Guid]::NewGuid().ToString('N').Substring(0,8)))
            & $editorCmd $projectFile '-ExecCmds=Automation RunTests BackyardMMO' '-TestExit=Automation Test Queue Empty' "-ReportExportPath=$reports" '-unattended' '-nop4' '-NullRHI' '-nosound' '-stdout' '-FullStdOutLogOutput'
            if ($LASTEXITCODE -ne 0) { throw "Unreal automation process failed (exit $LASTEXITCODE)." }
            $reportFile = Join-Path $reports 'index.json'
            if (-not (Test-Path -LiteralPath $reportFile)) { throw 'Automation did not produce its JSON report. Tests are not verified.' }
            $report = Get-Content -LiteralPath $reportFile -Raw | ConvertFrom-Json
            if (-not ($report.PSObject.Properties.Name -contains 'failed') -or -not ($report.PSObject.Properties.Name -contains 'succeeded')) { throw 'Unrecognized automation report. Tests are not verified.' }
            $testSourceRoot = Join-Path $projectRoot 'Plugins\BackyardMMO\Source'
            $expectedTests = @(Get-ChildItem -LiteralPath $testSourceRoot -Recurse -File -Filter '*.cpp' | Select-String -Pattern 'IMPLEMENT_SIMPLE_AUTOMATION_TEST\(').Count
            if ($expectedTests -lt 1) { throw 'The native test source inventory is missing. Tests are not verified.' }
            if ($report.failed -gt 0 -or $report.succeeded -lt $expectedTests) { throw "Unreal automation failed or did not run all $expectedTests BackyardMMO tests. Inspect TestResults." }
            foreach ($pendingField in @('inProcess','notRun')) {
                if (($report.PSObject.Properties.Name -contains $pendingField) -and $report.$pendingField -gt 0) { throw 'Unreal automation did not finish all selected tests. Inspect TestResults.' }
            }
            Write-Host "Automation report: $reportFile" -ForegroundColor Green
        } else {
            Write-Host 'Opening the prepared project.' -ForegroundColor Green
            Start-Process -FilePath $editor -ArgumentList @('"' + $projectFile + '"') -WorkingDirectory $projectRoot | Out-Null
        }
    }
} catch {
    Write-Host $_.Exception.Message -ForegroundColor Red
    Write-Host "Full log: $logFile" -ForegroundColor Yellow
    exit 1
} finally { Stop-Transcript | Out-Null }
