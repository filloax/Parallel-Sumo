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

- spider0, spider1, spider2.sumocfg: generated using our 
  randomSimulation.sh script with args
  `randomSimulation.sh spiderN  --spider` and an increasing size
  using params detailed here https://sumo.dlr.de/docs/netgenerate.html

- grid_large.sumocfg: generated using randomSimulation.sh
  with args `randomSimulation.sh grid_large --grid --grid.number 100 --grid.length 20`