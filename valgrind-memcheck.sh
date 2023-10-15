CFG="assets/simpleNet.sumocfg"
N=2

./run-with-env.sh python scripts/createParts.py -n $N -C "$CFG"

valgrind --tool=memcheck --leak-check=full --suppressions=vg.supp ./bin/main -N $N -c "$CFG" --skip-part