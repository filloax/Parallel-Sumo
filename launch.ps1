if (!(Get-Command "conda" -errorAction SilentlyContinue))
{
    Write-Host "Anaconda/Miniconda not installed, or didn't launch through its shell.", "`n",
        "For the program to work, it needs to be launched through the Conda command line,",
        "if you do not have conda installed you can get it at https://www.anaconda.com/download"
    exit
}

$Root = "$(Split-Path -Parent $MyInvocation.MyCommand.Path)"
$condaEnvPath = "$Root/.conda"

Write-Host "Conda env at $condaEnvPath"

if (!(Test-Path -Path "data" -PathType Container)) {
    New-Item -Type Directory -Path "data"
}

if (!(Test-Path -Path $condaEnvPath -PathType Container)) {
    & ./scripts/install-pymetis.ps1
} else {
    Write-Host "Activating $condaEnvPath"
    conda activate $condaEnvPath
}

Write-Host "Launching main"
bin/ParallelTwin.exe $args