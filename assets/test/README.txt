This folder should contain non-git tracked (for size matters) simulation files.

Current tests include the following simulations generated with these parameters.
Networks generated with OSM wizard include the osm.netcfg and associated bbox file
for the generation parameters.

- bologna-sim:
    - bologna-sim/osm.net.xml.gz and associated files: generated with 
      SUMO OSM WebWizard, containing the upper right quadrant of Bologna's
      city center (see included netcfg).
    - bologna-sim/osm.passenger.trips.xml: generated with SUMO randomTrips.py
      with params 
      `-n NETFILE -r osm.passenger.trips.xml -e 3600 --fringe-factor 20 --random-depart`
      and following with SUMO duarouter

- spider0: generated with `scripts/randomSimulation.sh test/spider0 --spider --spider.omit-center --spider.circle-number 3 --spider.arm-number 5`

- spider1: generated with `scripts/randomSimulation.sh test/spider1 --spider --spider.omit-center --spider.circle-number 6 --spider.arm-number 8`

- spider1: generated with `scripts/randomSimulation.sh test/spider2 --spider --spider.omit-center --spider.circle-number 15 --spider.arm-number 9`

- grid_large.sumocfg: generated with `scripts/randomSimulation.sh grid_large --grid --grid.number 100 --grid.length 20`

In one copypastable block:
```
scripts/randomSimulation.sh test/spider0 --spider --spider.omit-center --spider.circle-number 3 --spider.arm-number 5
scripts/randomSimulation.sh test/spider1 --spider --spider.omit-center --spider.circle-number 6 --spider.arm-number 8
scripts/randomSimulation.sh test/spider2 --spider --spider.omit-center --spider.circle-number 15 --spider.arm-number 9
scripts/randomSimulation.sh test/grid_large --grid --grid.number 100 --grid.length 20
```