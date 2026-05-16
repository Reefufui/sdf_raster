import json
import os
import sys

def update_resolution(base_dir, width, height):
    models = ["Napoleon", "Buggy", "Stone", "Radiator", "BMWManifold", "HumanSkeleton", "Buddha", "Dragon"]

    for model in models:
        config_path = os.path.join(base_dir, model, "original", "raster.json")

        if not os.path.exists(config_path):
            print(f"Skipping: {config_path} not found.")
            continue

        try:
            with open(config_path, 'r') as f:
                data = json.load(f)

            data["settings"]["window_width"] = int(width)
            data["settings"]["window_height"] = int(height)
            data["scene_states"][data["current_scene_path"]]["fixed_lod"] = 16               
            data["scene_states"][data["current_scene_path"]]["octree_depth"] = 16            
            data["scene_states"][data["current_scene_path"]]["frustum_culling_level"] = 16   
            data["scene_states"][data["current_scene_path"]]["occlusion_culling_level"] = 16 

            with open(config_path, 'w') as f:
                json.dump(data, f, indent=4)

            print(f"Successfully updated {model} to {width}x{height}")

        except Exception as e:
            print(f"Error processing {model}: {e}")

if __name__ == "__main__":
    if len(sys.argv) != 4:
        print("Usage: python3 update_resolution.py <base_dir> <width> <height>")
        sys.exit(1)

    update_resolution(sys.argv[1], sys.argv[2], sys.argv[3])
