import subprocess
import re
import sys, os, platform

passed_args = sys.argv[1:]

filter_output = True

# Run the .launch.sh script with arguments and capture the output
if platform.system() == "Windows":
    args = ["PowerShell", "./launch.ps1", *passed_args]
else:
    args = ["bash", "./launch.sh", *passed_args]

# Use regular expressions to match and extract values from Performance blocks
part_time_pattern = r'Partitioning took ([\d\.]+)ms!'
time_pattern = r'Parallel simulation took ([\d\.]+)ms!'

part_time, time = -1, -1

if filter_output:
    process = subprocess.Popen(args, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)

    stop = False
    while not stop:
        output, err = process.communicate()

        if not output and not err:
            break

        if output:
            if part_time < 0:
                part_match = re.search(part_time_pattern, output, re.MULTILINE)
                if part_match:
                    part_time = float(part_match.group(1))
            
            if time < 0:
                time_match = re.search(time_pattern, output, re.MULTILINE)
                if time_match:
                    time = float(time_match.group(1))

        if err and not 'warn' in err.lower():
            print(err, file=sys.stderr)

        if process.returncode is not None:
            stop = True

    # Wait for the process to complete, just in case
    process.wait()

    # Check the return code
    if process.returncode != 0:
        print(f"Error: The command failed with a non-zero exit code: {process.returncode}", file=sys.stderr)
        print(f'-99, -99')
    else:
        print(f'{time}, {part_time}')
else:
    output = subprocess.check_output(args, universal_newlines=True)

    part_match = re.search(part_time_pattern, output, re.MULTILINE)
    time_match = re.search(time_pattern, output, re.MULTILINE)
    
    if part_match:
        part_time = float(part_match.group(1))
    if time_match:
        time = float(time_match.group(1))

    print(f'{time}, {part_time}')