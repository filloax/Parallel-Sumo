$URL_64 = "https://download.lfd.uci.edu/pythonlibs/archived/PyMetis-2020.1-cp310-cp310-win_amd64.whl"
$URL_32 = "https://download.lfd.uci.edu/pythonlibs/archived/PyMetis-2020.1-cp310-cp310-win32.whl"

$URL = $URL_64

$PythonPath = Read-Host "Enter the path to Python 3.10 executable (e.g., C:\Python310\python.exe)"

$Root = "$(Split-Path -Parent $MyInvocation.MyCommand.Path)/.."
$AssetFolder = Join-Path -Path $Root -ChildPath "asset\py"
if (-not (Test-Path -Path $AssetFolder -PathType Container)) {
    New-Item -Path $AssetFolder -ItemType Directory
}

$FileName = [System.IO.Path]::GetFileName($URL)
$DestinationFile = Join-Path -Path $AssetFolder -ChildPath $FileName

Write-Host "Downloading $URL..."
Invoke-WebRequest -Uri $URL -OutFile $DestinationFile

# Create a Python virtual environment (.venv) if not present and activate it
$VenvFolder = Join-Path -Path $Root -ChildPath ".venv"
if (-not (Test-Path -Path $VenvFolder -PathType Container)) {
    & $PythonPath -m venv $VenvFolder
}
# Activate the virtual environment
& "$VenvFolder\Scripts\Activate.ps1"

# Install the downloaded file assuming it's a wheel (.whl) file
if ($FileName -match "\.whl$") {
    pip install $DestinationFile
} else {
    Write-Host "The downloaded file is not a .whl file and will not be installed."
}