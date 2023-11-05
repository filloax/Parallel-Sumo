from math import ceil, floor
import os, sys
import subprocess
import platform
import csv
from typing import TypedDict
import pandas as pd
import datetime
import shutil
import itertools
from PIL import Image, ImageDraw, ImageFont
import json

test_sumocfg = "assets/bologna-sim/osm.sumocfg"

num_parts = [
    4,
]

edge_weights = [
    "route-num",
    "osm",
]

node_weights = [
    "connections",
    "connexp"
]


test_dir = ""

class RunArgs(TypedDict):
    sumo_cfg: str
    num_parts: int
    edge_wgts: list[str]
    node_wgts: list[str]
    id: int

def _get_dir(args: RunArgs):
    cfgname = '_'.join(args["sumo_cfg"].split(os.sep)[-2:]).replace(".sumocfg", "")
    cdirn = f"""{args["id"]:2}_N{args["num_parts"]}___E{'-'.join(args["edge_wgts"])}_V{'-'.join(args["node_wgts"])}_{cfgname}"""
    return os.path.join(test_dir, cdirn)

def run_test(args: RunArgs):
    if platform.system() == "Windows":
        proc_args = ["PowerShell", "./launch.ps1"]
    else:
        proc_args = ["bash", "./launch.sh"]
        
    proc_args.extend([
        "-N", str(args["num_parts"]),
        "-c", args["sumo_cfg"],
        "--pin-to-cpu",
        "--log-handled-vehicles",
        "--",
        "--png", "--quick-png",
    ])
    if len(args["edge_wgts"]) > 0:
        for wgt in args["edge_wgts"]:
            proc_args.extend(["-w", wgt])
    else:
        # explicitly specify no edge weights
        proc_args.append("-w")
        
    if len(args["node_wgts"]) > 0:
        for wgt in args["node_wgts"]:
            proc_args.extend(["-W", wgt])
    else:
        # explicitly specify no node weights
        proc_args.append("-W")
        
    cdir = _get_dir(args)
    os.makedirs(cdir, exist_ok=True)
  
    with open(os.path.join(cdir, "command.txt"), "w") as f:
        print(' '.join(proc_args), file=f)
    with open(os.path.join(cdir, "args.json"), "w") as f:
        json.dump(args, f, indent=4)
        
    print("Running with command:", ' '.join(proc_args))
    with open(os.path.join(cdir, "log.txt"), "w") as f:
        subprocess.run(proc_args, check=True,
            stdout=f,
            stderr=subprocess.STDOUT,
        )
    
def gather_outputs(args: RunArgs):
    cdir = _get_dir(args)
    os.makedirs(cdir, exist_ok=True)
    
    copy_list = [
        "allStepVehicles.csv",
        "allStepVehicles.png",
        "simtimes.csv",
        "commtimes.csv",
        "partitions.png",
    ]
    
    for item in copy_list:
        shutil.copyfile(os.path.join("output", item), os.path.join(cdir, item))
    
    simtimes_df = pd.read_csv("output/simtimes.csv")
    commtimes_df = pd.read_csv("output/commtimes.csv")
    
    # Generate results page
    html_content = f"""
    <html>
    <head>
        <title>Results</title>
    </head>
    <body>
        <h2>Input Parameters</h2>
        <p>SUMO config: {args["sumo_cfg"]}</p>
        <p>N° Partitions: {args["num_parts"]}</p>
        <p>Edge Weighting Methods: {args["edge_wgts"]}</p>
        <p>Node Weighting Methods: {args["node_wgts"]}</p>

        <h2>Output</h2>
        <h3>Partitions</h3>
        <img src="./partitions.png" alt="partitions.png" width="800">
        
        <h3>Simulation times per partition</h3>
        {simtimes_df.to_html(classes='table table-striped', index=False)}
        <h3>Interaction times per partition</h3>
        {commtimes_df.to_html(classes='table table-striped', index=False)}
        
        <h3>Vehicles in partition per step</h3>
        <img src="./allStepVehicles.png" alt="allStepVehicles.png" width="800">
    </body>
    </html>
    """
    
    with open(os.path.join(cdir, "output.html"), 'w') as html_file:
        html_file.write(html_content)
        
    print("  Saved results to", cdir)
    
def _make_grid(suffix: str, images: list[Image.Image], image_names: list[str], image_args: list[RunArgs]):
    grid_width = min(4, floor(len(images) ** 0.5))
    grid_height = ceil((len(images) + 1) / grid_width)

    cell_width = max(img.width for img in images)
    cell_height = max(img.height for img in images)
    font_img_fraction = 0.08
    padding = int(cell_height * font_img_fraction)
    total_width = grid_width * (cell_width + padding)
    total_height = grid_height * (cell_height + padding)

    grid_image = Image.new('RGB', (total_width, total_height), (255, 255, 255))
    draw = ImageDraw.Draw(grid_image)
    
    breakpoint = font_img_fraction * cell_height
    fontsize = 1
    jumpsize = 75
    font = ImageFont.load_default(fontsize)
    while True:
        _, _, _, height = draw.textbbox((0, 0), "Sample text go!", font=font)
        if height < breakpoint:
            fontsize += jumpsize
        else:
            jumpsize = jumpsize // 2
            fontsize -= jumpsize
        font = ImageFont.load_default(fontsize)
        if jumpsize <= 1:
            break

    for i, (img, args) in enumerate(zip(images, image_args)):
        x = (i % grid_width) * (cell_width + padding)
        y = (i // grid_width) * (cell_height + padding)
        
        grid_image.paste(img, (x, y))
        
        label = f'{args["id"]} E: {",".join(args["edge_wgts"])} V: {",".join(args["node_wgts"])}'
        _, _, tw, th = draw.textbbox((0, 0), label, font=font)
        draw.text((x + (cell_width - tw) / 2, y + cell_height), label, fill=(0, 0, 0), font=font)

    grid_image.save(os.path.join(test_dir, f"stepVehiclesGrid{suffix}.png"))
    print("Grid image saved as", os.path.join(test_dir, f"stepVehiclesGrid{suffix}.png"))

    
def generate_grids():
    print("Creating image grids...")
    
    subdirs = [f for f in os.listdir(test_dir) if os.path.isdir(os.path.join(test_dir, f))]
    
    images = []
    image_names = []
    image_args: list[RunArgs] = []

    for i, dirname in enumerate(subdirs):
        dirpath = os.path.join(test_dir, dirname)
        image_path = os.path.join(dirpath, "allStepVehicles.png")
        images.append(Image.open(image_path))
        image_names.append(dirname)
        with open(os.path.join(dirpath, "args.json"), 'r') as f:
            image_args.append(json.load(f))

    args_dict_of_list = {key: [i[key] for i in image_args] for key in image_args[0]}
    # lazy to use pandas here, but it works
    image_data = pd.DataFrame({
        'image': images,
        'name': image_names,
        'args': image_args,
        **args_dict_of_list
    })
    
    for group, data in image_data.groupby("num_parts"):
        sdata = data.sort_values("id")
        _make_grid(f"_{group}", sdata["image"], sdata["name"], sdata["args"])
    
    
def main():
    global test_dir
    
    now = datetime.datetime.now()
    # test_dirn = "2023-11-4_23-7-50"
    test_dirn = f"ptest_{now.year}-{now.month}-{now.day}_{now.hour}-{now.minute}-{now.second}"
    test_dir = os.path.join("testResults", test_dirn)
    rootdir = os.path.abspath(os.path.join(os.path.dirname(__file__), os.pardir))
    os.chdir(rootdir)
    
    os.makedirs(test_dir, exist_ok=True)
    
    with open(os.path.join(test_dir, "info.txt"), "w") as f:
        print(f"SUMO cfg: {test_sumocfg}", file=f)

    edge_weights_sets = []
    for r in range(len(edge_weights) + 1):
        edge_weights_sets.extend(list(itertools.combinations(edge_weights, r)))
    node_weight_sets = []
    for r in range(len(node_weights) + 1):
        node_weight_sets.extend(list(itertools.combinations(node_weights, r)))

    print(f"Running tests for cfg {test_sumocfg}")
    print("Parameters:")
    print(f"\tPartition numbers: {num_parts}")
    print(f"\tEdge weight methods: {edge_weights}")
    print(f"\tNode weight methods: {node_weights}")
    print(f"Will run {len(edge_weights_sets) * len(node_weight_sets)} weights combinations, for a total of {len(edge_weights_sets) * len(node_weight_sets) * len(num_parts)} configurations")
    print("==============================")
        
    print("Starting!")
        
    i = 0
    for N in num_parts:
        for ewgts, vwgts in itertools.product(edge_weights_sets, node_weight_sets):
            args = RunArgs({
                "num_parts": N,
                "sumo_cfg": test_sumocfg,
                "edge_wgts": ewgts,
                "node_wgts": vwgts,
                "id": i,
            })
            run_test(args)
            gather_outputs(args)
            i+=1
        
    print("Done!")
    
    generate_grids()
        

if __name__ == '__main__':
    main()