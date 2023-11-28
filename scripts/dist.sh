#!/bin/bash

DIST_NAME=$1
if [ -z $DIST_NAME ]; then
    DIST_FILE="dist/parsumo.tar.zip"
else
    DIST_FILE="dist/parsumo_$DIST_NAME.zip"
fi

PROJ_DIR=$( cd -- "$( dirname -- "${BASH_SOURCE[0]}" )"/.. &> /dev/null && pwd )

cd "$PROJ_DIR"
mkdir -p dist

DST_FOLDER="parsumo"
mkdir -p "$DST_FOLDER"

echo Copying scripts...
mkdir -p "$DST_FOLDER/scripts"
cp scripts/*.sh "$DST_FOLDER/scripts/"
cp scripts/*.py "$DST_FOLDER/scripts/"
cp scripts/*.txt "$DST_FOLDER/scripts/"
cp launch.sh "$DST_FOLDER/"
cp run-with-env.sh "$DST_FOLDER/"
cp performance-measures-launch.sh "$DST_FOLDER/"

echo Copying some example assets...
mkdir -p "$DST_FOLDER/examples"
cp assets/simpleNet.* "$DST_FOLDER/examples/"
cp assets/gui.settings.xml "$DST_FOLDER/examples/"
cp assets/testNet2.* "$DST_FOLDER/examples/"
cp assets/spider0.* "$DST_FOLDER/examples/"
# cp -r assets/osm-bologna-ing "$DST_FOLDER/examples/"

echo Copying binaries...
cp bin/ParallelTwin* "$DST_FOLDER/"
cp DIST_README.md "$DST_FOLDER/README.txt"

echo Zipping...
zip -r "$DIST_FILE" parsumo

rm -rf parsumo

echo "Saved to: $(realpath "$DIST_FILE")"