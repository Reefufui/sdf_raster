#!/bin/bash

BASE_DIR="fps_comparation"
MODELS=("BMWManifold" "Buddha" "Buggy" "Dragon" "HumanSkeleton" "Napoleon" "Radiator" "Stone")
LEVELS=("original" "decimation_01" "decimation_05" "decimation_1" "decimation_5" "scomtree_07" "scomtree_08" "scomtree_09" "scomtree_10" "scomtree_11")

MISSING_COUNT=0

for model in "${MODELS[@]}"; do
    for level in "${LEVELS[@]}"; do
        DIR="$BASE_DIR/$model/$level"
        if [ ! -d "$DIR" ]; then
            echo "Missing Directory: $DIR"
            ((MISSING_COUNT++))
            continue
        fi
    done
done

if [ $MISSING_COUNT -eq 0 ]; then
    echo "Validation successful: All expected files are present in the experiment directories."
else
    echo "Validation failed: $MISSING_COUNT items missing."
fi
