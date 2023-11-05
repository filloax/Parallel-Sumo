import pandas as pd
import glob
from matplotlib import pyplot as plt

def work(prefix: str, out: str):
    file_paths = glob.glob(f"data/{prefix}*.txt")
    data = {}
    for file_path in file_paths:
        column_name = int(file_path.split(prefix)[1].split(".txt")[0])
        with open(file_path, 'r') as f:
            value = float(f.read()) / 1000 #seconds
        data[column_name] = [value]

    df = pd.DataFrame(data)
    df = df.reindex(sorted(df.columns), axis=1)
    df = df.rename(columns={val: f'p{val}' for val in df.columns})
    df.to_csv(f"output/{out}.csv")
    print(f"Saved output/{out}.csv")


# Simulation times
work("simtime", "simtimes")
    
# Interaction times
work("commtime", "commtimes")
