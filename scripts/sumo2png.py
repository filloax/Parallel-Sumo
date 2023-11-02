import os, sys
from itertools import cycle, islice
import json
import contextily as cx
from matplotlib import pyplot as plt
from PIL import Image
import geopandas as gpd
import pandas as pd
import matplotlib.pyplot as plt
import matplotlib.colors as mcolors
import matplotlib.patches as mpatches
import matplotlib.patheffects as patheffects
from matplotlib.legend_handler import HandlerTuple
import numpy as np
from xml.etree import ElementTree as ET
from shapely.ops import transform

from sumobin import run_net2geojson

simple_part_colors = ['red', 'blue', 'green', 'black', 'purple', 'cyan', 'magenta', 'orange', 'teal', 'yellow', 'crimson', 'lime']

def _net_to_gdf(net_file: str, temp_files: set[str], data_folder: str):
    tree = ET.parse(net_file)
    root = tree.getroot()
    location = root.find(".//location")
    has_location_data = True
    modified = False
    if location is None:
        has_location_data = False
        location = ET.Element("location")
        location.attrib["netOffset"] = "0,0"
        location.attrib["convBoundary"] = "0,0,3000,3000"
        location.attrib["origBoundary"] = "0,0,3000,3000"
        root.append(location)
        modified = True
    if "projParameter" not in location.attrib or location.attrib["projParameter"] == "!":
        has_location_data = False
        location.attrib["projParameter"] = "+proj=utm +zone=32 +ellps=WGS84 +datum=WGS84 +units=m +no_defs"
        modified = True
    if modified:
        tree.write(net_file)
        print("Added fixed location data to network file")

    name = os.path.basename(net_file).replace('.net.xml', '')
    geo_json_path = os.path.join(data_folder, f"{name}.geo.json")
    try:
        run_net2geojson(net_file, geo_json_path, ['--junctions'])
    except:
        print("[ERR] Network doesn't provide geo json representation, cannot save image...", file=sys.stderr)
        return None, True
    temp_files.add(geo_json_path)
    
    gdf: gpd.GeoDataFrame = gpd.read_file(geo_json_path)

    return gdf, has_location_data

def generate_network_image(net_files: list[str], output_png_file:str, data_folder:str, temp_files: set[str], edge_value_file: str = None):
    fig, ax = plt.subplots(figsize=(15,15))
    
    part_colors = simple_part_colors

    edge_value_dict = None
    if edge_value_file:
        with open(edge_value_file, 'r', encoding='utf-8') as f:
            edge_value_dict = json.load(f)

    has_location_data = True

    for net_file, color in zip(net_files, cycle(part_colors)):
        gdf, _has_loc_data = _net_to_gdf(net_file, temp_files, data_folder)
        has_location_data = has_location_data and _has_loc_data
        if edge_value_dict:
            gdf["value"] = gdf.id.apply(lambda id: edge_value_dict.get(id, -1))
            gdf.plot("value", ax=ax)#, legend=True)
        else:
            gdf.plot(ax=ax, color=color)

    if has_location_data:
        print("Adding background with contextily...")
        cx.add_basemap(ax, crs='epsg:4326', source=cx.providers.OpenStreetMap.Mapnik, alpha=0.8)
    else:
        print("Cannot add background with contextily, not real road network")
    
    plt.savefig(output_png_file, dpi=300, bbox_inches='tight')
    plt.close()
    print(f"Saved network png to {output_png_file}")
    return output_png_file

def _offset_downwards(geometry, offset):
    return transform(lambda x, y: (x, y - offset), geometry)

def _merge_partition_gdfs(gdfs: list[gpd.GeoDataFrame], alt_dashes = False) -> gpd.GeoDataFrame:
    for i, gdf in enumerate(gdfs):
        gdf["partitions"] = i
    
    merged_gdf = pd.concat(gdfs, ignore_index=True)
    merged_gdf = merged_gdf.groupby('id') \
        .agg({'partitions': tuple, **{col: 'first' for col in merged_gdf.columns if col != 'partitions' and col != 'id'}}) \
        .reset_index()
    merged_gdf = gpd.GeoDataFrame(merged_gdf, geometry="geometry")
    merged_gdf
    
    # cmap = plt.cm.Dark2
    # partcolors = [None for _ in gdfs]
    # for i in range(len(gdfs)):
    #     partcolors[i] = cmap(i)
    partcolors = list(islice(cycle(simple_part_colors), len(gdfs)))

    offset = 0.00004
    exploded_rows = []
    linewidth = 1.5
    
    props_to_set = ['color', 'linestyle']#, 'linewidth']
    props = {prop: [] for prop in props_to_set}

    for index, row in merged_gdf.iterrows():
        # For now one row for each, but can be changed to have more rows (and so plotted geometries)
        # for each partition in case
        
        geometry = row['geometry']
        partitions = row['partitions']

        if alt_dashes:
            for i in range(min(2, len(partitions))):
                part = partitions[i]
                new_row = {
                    'id': row['id'], 'geometry': geometry, 'partitions': (part,),
                }
                # geometry = _offset_downwards(geometry, offset)
                
                color = partcolors[part]
                props['color'].append(color)
                if i == 0:
                    props['linestyle'].append('solid' if len(partitions) == 1 else (0, (5, 5)))
                else:
                    # dashed other way around
                    props['linestyle'].append((5, (5, 5)))
                exploded_rows.append(new_row)
        else:
            part = partitions[i]
            new_row = {
                'id': row['id'], 'geometry': geometry, 'partitions': (part,),
            }            
            colors = [color for i, color in enumerate(partcolors) if i in partitions]
            colors_hsl = [mcolors.rgb_to_hsv(mcolors.to_rgba(name)[:-1]) for name in colors]
            # colors_hsl = [mcolors.rgb_to_hsv(color[:3]) for color in colors]
            average_hsl = np.mean(colors_hsl, axis=0)
            average_rgb = mcolors.hsv_to_rgb(average_hsl)
            color = average_rgb
            props['color'].append(color)
            props['linestyle'].append('solid' if len(partitions) == 1 else 'dashed')
            exploded_rows.append(new_row)

    
    return gpd.GeoDataFrame(exploded_rows).reset_index(drop=True), props, partcolors

def generate_partitions_image(net_files: list[str], output_png_file:str, data_folder:str, temp_files: set[str], better_dashes = False):
    fig, ax = plt.subplots(figsize=(15,15))

    has_location_data = True
    
    gdfs = []
    for net_file in net_files:
        gdf, _has_loc_data = _net_to_gdf(net_file, temp_files, data_folder)
        has_location_data = has_location_data and _has_loc_data
        gdfs.append(gdf)
        
    final_gdf, draw_props, partcolors = _merge_partition_gdfs(gdfs, better_dashes)
            
    if better_dashes:
        for i, (index, row) in enumerate(final_gdf.iterrows()):
            this_props = {prop: draw_props[prop][i] for prop in draw_props}
            gpd.GeoSeries({'geometry': row['geometry']}).plot(ax=ax, **this_props, path_effects=[patheffects.SimpleLineShadow(shadow_color="black", linewidth=1),patheffects.Normal()])
    else:
        final_gdf.plot(ax=ax, **draw_props, path_effects=[patheffects.SimpleLineShadow(shadow_color="black", linewidth=1),patheffects.Normal()])

    # gdf["centroid"] = gdf.to_crs('+proj=cea').geometry.centroid.to_crs(gdf.crs)
    # gdf[gdf.element == "junction"]["centroid"].plot(ax=ax, color=color, markersize=3, edgecolors='black', linewidth=0.5)
        
    if has_location_data:
        print("Adding background with contextily...")
        cx.add_basemap(ax, crs='epsg:4326', source=cx.providers.OpenStreetMap.Mapnik, alpha=0.6)
    else:
        print("Cannot add background with contextily, not real road network")
    
    # Add labels for each partition color
    partition_labels = []
    for i, color in enumerate(partcolors):
        label = f"Partition {i}"
        partition_labels.append(mpatches.Patch(color=color, label=label))
        
    ax.legend(handles=partition_labels, title='Partitions')
    
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

