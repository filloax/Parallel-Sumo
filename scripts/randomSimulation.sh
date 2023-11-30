#!/bin/bash

# Generate random SUMO simulation and run it,
# mainly using netgenerate to decide on the path

# First arg is the name of the network/route/cfg files, second
# is the number of trips to be generated, the rest are passed to netgenerate

NAME="$1"
shift

WORKDIR="./assets"
NETFILE="$WORKDIR/$NAME.net.xml"
ROUFILE="$WORKDIR/$NAME.rou.xml"
ROUFILE2="$WORKDIR/$NAME""_lowtraffic.rou.xml"
CFGFILE="$WORKDIR/$NAME.sumocfg"
CFGFILE2="$WORKDIR/$NAME""_lowtraffic.sumocfg"

END_TIME="3600"

"$SUMO_HOME/bin/netgenerate" -o "$NETFILE" "$@"

python "$SUMO_HOME/tools/randomTrips.py" -n "$NETFILE" -r "$ROUFILE" -e "$END_TIME" --fringe-factor 20 --random-depart
python "$SUMO_HOME/tools/randomTrips.py" -n "$NETFILE" -r "$ROUFILE2" -e "$END_TIME" --fringe-factor 20 --random-depart --period 10

"$SUMO_HOME/bin/sumo" -n "$NETFILE" -r "$ROUFILE" -b 0 -e "$END_TIME" --save-configuration "$CFGFILE"
"$SUMO_HOME/bin/sumo" -n "$NETFILE" -r "$ROUFILE2" -b 0 -e "$END_TIME" --save-configuration "$CFGFILE2"