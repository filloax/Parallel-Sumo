import subprocess
import re
import sys

passed_args = sys.argv[1:]

# Run the .launch.sh script with arguments and capture the output
args = ["PowerShell", "./launch.ps1", *passed_args, "--verbose"]
output = subprocess.check_output(args, universal_newlines=True)

# with open("output.txt", "w") as f:
#     print(output, file=f)

# Initialize variables to store the aggregated values
total_duration = 0
total_traci_duration = 0
total_real_time_factor = 0
total_ups = 0
performance_block_count = 0

# Use regular expressions to match and extract values from Performance blocks
performance_pattern = r"""Performance:\s*
\s*Duration:\s*(\d+\.\d+)s\s*
\s*TraCI-Duration:\s*(\d+\.\d+)s\s*
\s*Real\s*time\s*factor:\s*(\d+\.\d+)\s*
\s*UPS:\s*(\d+\.\d+)\s*
"""

matches = re.finditer(performance_pattern, output, re.MULTILINE)

# Process each match and calculate the averages
for i, match in enumerate(matches):
    duration = float(match.group(1))
    traci_duration = float(match.group(2))
    real_time_factor = float(match.group(3))
    ups = float(match.group(4))

    total_duration += duration
    total_traci_duration += traci_duration
    total_real_time_factor += real_time_factor
    total_ups += ups
    performance_block_count += 1

# Calculate the averages
avg_duration = total_duration / performance_block_count
avg_traci_duration = total_traci_duration / performance_block_count
avg_real_time_factor = total_real_time_factor / performance_block_count
avg_ups = total_ups / performance_block_count

# Create a CSV row with the averaged values
csv_row = f"{avg_duration:.2f},{avg_traci_duration:.2f},{avg_real_time_factor:.2f},{avg_ups:.2f}"

# Print the CSV row
print(csv_row)