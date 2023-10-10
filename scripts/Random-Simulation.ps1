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

Write-Host "Generating network $NETFILE..."

& "$env:SUMO_HOME/bin/netgenerate" -o "$NETFILE" $args

Write-Host "Generating random trips $ROUFILE..."

python "$env:SUMO_HOME/tools/randomTrips.py" -n "$NETFILE" -r "$ROUFILE" -e "$END_TIME"

Write-Host "Writing config $CFGFILE..."

& "$env:SUMO_HOME/bin/sumo" -n "$NETFILE" -r "$ROUFILE" -b 0 -e "$END_TIME" --save-configuration "$CFGFILE"
