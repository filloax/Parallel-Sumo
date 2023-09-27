import os, sys
from itertools import cycle
import json
import contextily as cx
from matplotlib import pyplot as plt
from PIL import Image
import geopandas as gpd
import matplotlib.pyplot as plt

from sumobin import run_net2geojson

def generate_network_image(net_files: list[str], output_png_file:str, data_folder:str, temp_files: set[str], edge_value_file: str = None):
    fig, ax = plt.subplots(figsize=(15,15))

    colors = ['red', 'blue', 'green', 'black', 'purple', 'orange', 'cyan', 'magenta', 'yellow']

    edge_value_dict = None
    if edge_value_file:
        with open(edge_value_file, 'r', encoding='utf-8') as f:
            edge_value_dict = json.load(f)

    for net_file, color in zip(net_files, cycle(colors)):
        name = os.path.basename(net_file).replace('.net.xml', '')
        geo_json_path = os.path.join(data_folder, f"{name}.geo.json")
        try:
            run_net2geojson(net_file, geo_json_path)
        except:
            print("[ERR] Network doesn't provide geo json representation, cannot save image...", file=sys.stderr)
            return None
        temp_files.add(geo_json_path)

        gdf: gpd.GeoDataFrame = gpd.read_file(geo_json_path)
        if edge_value_dict:
            gdf["value"] = gdf.id.apply(lambda id: edge_value_dict.get(id, -1))
            gdf.plot("value", ax=ax)#, legend=True)
        else:
            gdf.plot(ax=ax, color=color)
    print("Adding background with contextily...")
    cx.add_basemap(ax, crs='epsg:4326', source=cx.providers.OpenStreetMap.Mapnik, alpha=0.8)
    plt.savefig(output_png_file, dpi=300, bbox_inches='tight')
    plt.close()
    print(f"Saved network png to {output_png_file}")
    return output_png_file

def create_grid_image(png_paths, grid_size, output_png_file):
    images = []

    for png in png_paths:
        images.append(Image.open(png))

    grid_width = min(grid_size[0], len(images))
    grid_height = min(grid_size[1], len(images))

    # Calculate the size of each cell in the grid
    cell_width = max(img.width for img in images)
    cell_height = max(img.height for img in images)

    grid_image = Image.new('RGB', (grid_width * cell_width, grid_height * cell_height), (255, 255, 255))

    for i, img in enumerate(images):
        x = (i % grid_width) * cell_width
        y = (i // grid_width) * cell_height
        grid_image.paste(img, (x, y))

    grid_image.save(output_png_file)
    print(f"Combined images saved as: {output_png_file}")

