# if (!(Test-Path -Path "data" -PathType Container)) {
#     New-Item -Type Directory -Path "data"
# }

# if (!(Test-Path -Path "data" -PathType Container)) {
#     python -m venv .venv
#     pip install -r "scripts/requirements.txt"
#     .venv/Scripts/activate
# } else {
#     .venv/Scripts/activate
# }

# bin/main.exe $args

Write-Host "To tweak to use anaconda; doesn't work for now"