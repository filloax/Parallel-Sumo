import pandas as pd
import glob
from matplotlib import pyplot as plt
import argparse

parser = argparse.ArgumentParser()
parser.add_argument("-s", "--start", type=int, default=None, help="Start of plot time range")
parser.add_argument("-e", "--end", type=int, default=None, help="End of plot time range")

args = parser.parse_args()

file_paths = glob.glob("data/msgNum*.csv")
data = {}
for file_path in file_paths:
    column_name = f'{int(file_path.split("msgNum")[1].split(".csv")[0]):03}'
    df = pd.read_csv(file_path)
    data[f'{column_name}_in'] = df["msgs_in"]
    data[f'{column_name}_out'] = df["msgs_out"]

df = pd.DataFrame(data)
df = df.reindex(sorted(df.columns), axis=1)
df = df.rename(columns={val: f'p{val}' for val in df.columns})
df.index.name = 'time'
df.to_csv("output/msgNum.csv")
print("Saved output/msgNum.csv")

start = args.start if args.start is not None else 0
end = args.end if args.end is not None else len(df)

print(f"Generating image with range [{start}, {end}]")

df = df.iloc[start:end]

time_units = df.index.to_series()

def get_data(col):
    if time_units.iloc[-1] < 200:
        # short enough to not use rolling max
        return df[col]
    else:
        return df.rolling(window=int(len(df)/80)).max()[col]

partitions = sorted(set(col.split('_')[0] for col in df.columns if col.endswith('_in')))
num_partitions = len(partitions)
fig, axes = plt.subplots(num_partitions, 1, sharex=True, figsize=(12, 2 * num_partitions))

colors = ['blue', 'red']

for i, partition in enumerate(partitions):
    ax = axes[i]
    for j, msg_type in enumerate(['_in', '_out']):
        ax.plot(time_units, get_data(f'{partition}{msg_type}'), label=f'Messages {msg_type}', color=colors[j])
    ax.set_title(f'Partition {partition}')
    ax.set_ylabel('Messages/step')
    ax.legend()

fig.text(0.5, 0.01, 'Time', ha='center')
fig.suptitle('Partition messages over time', y=1.02)

plt.tight_layout()

plt.savefig("output/msgNum.png", bbox_inches='tight')
print("Saved output/msgNum.png")

# Plot the rolling average for all columns
# fig1, ax1 = plt.subplots(figsize=(12, 6))
# differences = df.apply(lambda col: col - df.mean(axis=1))
# df.mean(axis=1).plot.line(ax=ax1, linewidth=2, color='k')
# # Plot the differences using a thicker line and separate y-axis on the right
# ax2 = ax1.twinx()
# differences.rolling(window=int(len(df)/40)).mean().plot.line(ax=ax2)

# plt.savefig("output/allStepVehiclesDiff.png", bbox_inches='tight')
# print("Saved output/allStepVehiclesDiff.png")