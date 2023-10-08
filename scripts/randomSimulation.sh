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
CFGFILE="$WORKDIR/$NAME.sumocfg"

END_TIME="1000"

"$SUMO_HOME/bin/netgenerate" -o "$NETFILE" "$@"

python "$SUMO_HOME/tools/randomTrips.py" -n "$NETFILE" -r "$ROUFILE" -e "$END_TIME"

"$SUMO_HOME/bin/sumo" -n "$NETFILE" -r "$ROUFILE" -e "$END_TIME" --save-configuration "$CFGFILE"