import os, sys
import subprocess

if 'SUMO_HOME' in os.environ:
    SUMO_HOME = os.environ['SUMO_HOME']
    net_tools = os.path.join(SUMO_HOME, 'tools', 'net')

    import sumolib

    DUAROUTER = sumolib.checkBinary('duarouter')
    NETCONVERT = sumolib.checkBinary('netconvert')
    # python script, but doesn't have an importable main function (everything in the if)
    NET2GEOJSON = os.path.join(net_tools, "net2geojson.py")
else:
    sys.exit("please declare environment variable 'SUMO_HOME'")

def run_netconvert(net_file: str, output: str, extra_options: list[str] = []):
    _run_prefix([
        NETCONVERT,
        *extra_options,
        "-s", net_file,
        "-o", output
    ], "netconvert | ")

def run_duarouter(net_file: str, trip_files: list[str], output: str, additional_files: list[str] = [], extra_options: list[str] = []):
    opts = [
        DUAROUTER,
        "--net-file", net_file, 
        "--route-files", ",".join(trip_files),
        "--output", output,
        *extra_options
    ]
    if len(additional_files) > 0:
        opts.extend([
            "--additional-files", ",".join(additional_files),
        ])
    _run_prefix(opts, "duarouter | ")

def run_net2geojson(net_file: str, output: str, extra_options: list[str] = []):
    _run_prefix([
        sys.executable, NET2GEOJSON,
        "--net-file", net_file, 
        "--output", output,
        *extra_options
    ], "net2geojson")

def _run_prefix(args: list[str], prefix: str = "PROC | ", **kwargs):
    print("Executing", " ".join(args))

    process = subprocess.Popen(args, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, **kwargs)

    while True:
        stdout_line = process.stdout.readline()
        stderr_line = process.stderr.readline()

        if not stdout_line and not stderr_line:
            break

        if stdout_line:
            # Prepend your prefix to stdout lines and print them
            print(prefix + stdout_line, end="")

        if stderr_line:
            # Handle stderr separately if needed
            print(prefix +stderr_line, end="", file=sys.stderr)

    # Wait for the process to complete
    process.wait()

    # Check the return code
    if process.returncode != 0:
        raise Exception(f"Error: The command failed with a non-zero exit code: {process.returncode}")