import pandas as pd
import glob
from matplotlib import pyplot as plt

file_paths = glob.glob("data/simtime*.txt")
data = {}
for file_path in file_paths:
    column_name = int(file_path.split("simtime")[1].split(".txt")[0])
    with open(file_path, 'r') as f:
        value = float(f.read()) / 1000 #seconds
    data[column_name] = [value]

df = pd.DataFrame(data)
df = df.reindex(sorted(df.columns), axis=1)
df = df.rename(columns={val: f'p{val}' for val in df.columns})
df.to_csv("output/simtimes.csv")
print("Saved output/simtimes.csv")