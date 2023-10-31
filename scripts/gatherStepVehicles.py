import pandas as pd
import glob
from matplotlib import pyplot as plt

file_paths = glob.glob("data/stepVehicles*.csv")
data = {}
for file_path in file_paths:
    column_name = int(file_path.split("stepVehicles")[1].split(".csv")[0])
    df = pd.read_csv(file_path)
    data[column_name] = df["vehNo"]

result = pd.DataFrame(data)
result = result.reindex(sorted(result.columns), axis=1)
result = result.rename(columns={val: f'p{val}' for val in df.columns})
result.to_csv("data/allStepVehicles.csv")
print("Saved data/allStepVehicles.csv")

fig, ax = plt.subplots(figsize=(30, 15))
result.rolling(window=int(len(result)/40)).mean().plot.line(ax=ax)
plt.savefig("data/allStepVehicles.png")
print("Saved data/allStepVehicles.png")
