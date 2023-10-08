# Generate random SUMO simulation and run it,
# mainly using netgenerate to decide on the path

# First arg is the name of the network/route/cfg files, second
# is the number of trips to be generated, the rest are passed to netgenerate

$NAME = $args[0]
$args = $args[1..($args.length - 1)]

$WORKDIR = "./assets"
$NETFILE = "$WORKDIR\$NAME.net.xml"
$ROUFILE = "$WORKDIR\$NAME.rou.xml"
$CFGFILE = "$WORKDIR\$NAME.sumocfg"

$END_TIME = "1000"

& "$env:SUMO_HOME/bin/netgenerate" -o "$NETFILE" $args

python "$env:SUMO_HOME/tools/randomTrips.py" -n "$NETFILE" -r "$ROUFILE" -e "$END_TIME"

& "$env:SUMO_HOME/bin/sumo" -n "$NETFILE" -r "$ROUFILE" -e "$END_TIME" --save-configuration "$CFGFILE"
