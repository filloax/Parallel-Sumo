import pandas as pd
import glob
from matplotlib import pyplot as plt

file_paths = glob.glob("data/stepVehicles*.csv")
data = {}
for file_path in file_paths:
    column_name = int(file_path.split("stepVehicles")[1].split(".csv")[0])
    df = pd.read_csv(file_path)
    data[column_name] = df["vehNo"]

df = pd.DataFrame(data)
df = df.reindex(sorted(df.columns), axis=1)
df = df.rename(columns={val: f'p{val}' for val in df.columns})
df.to_csv("output/allStepVehicles.csv")
print("Saved output/allStepVehicles.csv")

fig, ax = plt.subplots(figsize=(12, 6))
df.rolling(window=int(len(df)/40)).mean().plot.line(ax=ax)
plt.savefig("output/allStepVehicles.png", bbox_inches='tight')
print("Saved output/allStepVehicles.png")


# Plot the rolling average for all columns
fig1, ax1 = plt.subplots(figsize=(12, 6))
differences = df.apply(lambda col: col - df.mean(axis=1))
df.mean(axis=1).plot.line(ax=ax1, linewidth=2, color='k')
# Plot the differences using a thicker line and separate y-axis on the right
ax2 = ax1.twinx()
differences.rolling(window=int(len(df)/40)).mean().plot.line(ax=ax2)

plt.savefig("output/allStepVehiclesDiff.png", bbox_inches='tight')
print("Saved output/allStepVehiclesDiff.png")