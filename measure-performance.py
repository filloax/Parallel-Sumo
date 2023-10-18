import subprocess
import re
import sys, os, platform

passed_args = sys.argv[1:]

# Run the .launch.sh script with arguments and capture the output
if platform.system() == "Windows":
    args = ["PowerShell", "./launch.ps1", *passed_args]
else:
    args = ["bash", "./launch.sh", *passed_args]
output = subprocess.check_output(args, universal_newlines=True)

# Use regular expressions to match and extract values from Performance blocks
part_time_pattern = r'Partitioning took ([\d\.]+)ms!'
time_pattern = r'Parallel simulation took ([\d\.]+)ms!'

part_match = re.search(part_time_pattern, output, re.MULTILINE)
time_match = re.search(time_pattern, output, re.MULTILINE)

part_time, time = -1, -1

if part_match:
    part_time = float(part_match.group(1))
if time_match:
    time = float(time_match.group(1))

# Print the CSV row
print(f'{time, part_time}')