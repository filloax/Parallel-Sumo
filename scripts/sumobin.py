import os, sys
import subprocess
import re

if 'SUMO_HOME' in os.environ:
    SUMO_HOME = os.environ['SUMO_HOME']
    net_tools = os.path.join(SUMO_HOME, 'tools', 'net')

    tools = os.path.join(os.environ['SUMO_HOME'], 'tools')
    sys.path.append(os.path.join(tools))

    import sumolib

    DUAROUTER = sumolib.checkBinary('duarouter')
    NETCONVERT = sumolib.checkBinary('netconvert')
    # python script, but doesn't have an importable main function (everything in the if)
    NET2GEOJSON = os.path.join(net_tools, "net2geojson.py")
else:
    sys.exit("please declare environment variable 'SUMO_HOME'")

def run_netconvert(net_file: str, output: str, extra_options: list[str] = [], mute_warnings: bool = False):
    _run_prefix([
        NETCONVERT,
        *extra_options,
        "-s", net_file,
        "-o", output,
    ], "netconvert | ", mute_warnings=mute_warnings)

def run_duarouter(net_file: str, trip_files: list[str], output: str, additional_files: list[str] = [], extra_options: list[str] = [], quiet = False):
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
    _run_prefix(opts, "duarouter | ", mute_stdout=quiet)

def run_net2geojson(net_file: str, output: str, extra_options: list[str] = []):
    _run_prefix([
        sys.executable, NET2GEOJSON,
        "--net-file", net_file, 
        "--output", output,
        *extra_options
    ], "net2geojson | ")

def _run_prefix(args: list[str], prefix: str = "PROC | ", mute_warnings: bool = False, mute_stdout: bool = False, **kwargs):
    print("Executing", " ".join(args))

    process = subprocess.Popen(args, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, **kwargs)

    printed_success = False
    stop = False
    while not stop:
        output, err = process.communicate()

        if not output and not err:
            break

        if output and not mute_stdout:
            # Prepend your prefix to stdout lines and print them
            print('\n'.join(prefix + o for o in output.split('\n')), end="")

        if err  and (not mute_warnings or not re.search(r'^[^\w]*warning', err.lower())):
            # Handle stderr separately if needed
            print('\n'.join(prefix + e for e in err.split('\n')), end="", file=sys.stderr)

        if process.returncode is not None:
            stop = True

    # Wait for the process to complete, just in case
    process.wait()

    # Check the return code
    if process.returncode != 0:
        if printed_success:
            print(prefix + f"[WARN] Printed Success but exited with non zero exit code {process.returncode}, will continue anyways", file=sys.stderr)
        else:
            print(f"Error: The command failed with a non-zero exit code: {process.returncode}, exiting", file=sys.stderr)
            sys.exit(process.returncode)
