import pandas as pd
import glob

file_paths = glob.glob("data/stepVehicles*.csv")
data = {}
for file_path in file_paths:
    column_name = int(file_path.split("stepVehicles")[1].split(".csv")[0])
    df = pd.read_csv(file_path)
    data[column_name] = df["vehNo"]

result = pd.DataFrame(data)
result = result.reindex(sorted(result.columns), axis=1)
result.to_csv("data/allStepVehicles.csv")
print("Saved data/allStepVehicles.csv")