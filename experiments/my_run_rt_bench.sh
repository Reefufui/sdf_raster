#!/bin/bash

BASE_DIR="$1"

if [ -z "$BASE_DIR" ]; then
    echo "Usage: $0 <path_to_experiments_dir>"
    echo "Example: $0 /home/andrey-tree/sdf_raster/experiments/fps_comparation"
    exit 1
fi

if [ ! -d "$BASE_DIR" ]; then
    echo "Error: Directory '$BASE_DIR' not found."
    exit 1
fi

MODELS=("Napoleon" "Buggy" "Stone" "Radiator" "BMWManifold" "HumanSkeleton" "Buddha" "Dragon")
LEVELS=("original" "decimation_5" "decimation_1" "decimation_05" "decimation_01" "scomtree_10" "scomtree_09" "scomtree_08" "scomtree_07")

for model in "${MODELS[@]}"; do
    for level in "${LEVELS[@]}"; do
        CONFIG_DIR="$BASE_DIR/$model/$level"
        echo "Running RT benchmark for $model ($level)..."
        ./render_app -render "$CONFIG_DIR/rt.blk" -u 10 -m 100
    done
done
