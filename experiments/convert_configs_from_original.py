import json
import os
import sys
import copy

MODELS = ["Napoleon", "Buggy", "Stone", "Radiator", "BMWManifold", "HumanSkeleton", "Buddha", "Dragon"]
LEVELS = ["original", "decimation_5", "decimation_1", "decimation_05", "decimation_01", "scomtree_11", "scomtree_10", "scomtree_09", "scomtree_08", "scomtree_07"]

def generate_configs(base_dir):
    for model in MODELS:
        master_path = os.path.join(base_dir, model, "original", "raster.json")
        if not os.path.exists(master_path):
            print(f"Skipping {model}: Master config not found at {master_path}")
            continue

        with open(master_path, 'r') as f:
            master_data = json.load(f)

        scene_path = master_data["current_scene_path"]
        litert_camera = master_data["scene_states"][scene_path]["camera"]["LiteRT"]
        fov_y = master_data["scene_states"][scene_path]["camera"]["fov_y"]
        
        lighting = master_data["settings"]["lighting"]
        width = master_data["settings"]["window_width"]
        height = master_data["settings"]["window_height"]
        ambient = lighting["ambient_strength"]

        for level in LEVELS:
            print(f"Generating config {model}:{level}...")
            config_dir = os.path.join(base_dir, model, level)
            os.makedirs(config_dir, exist_ok=True)

            level_raster_data = copy.deepcopy(master_data)
            
            if "scom" in level:
                level_scene_path = scene_path.replace("original", level).replace(f".obj", "/scom2_0.bin")

                level_raster_data["scene_states"][scene_path]["draw_method"] = 8
                level_raster_data["settings"]["lighting"]["depth_threshold"] = 4e-05
            else:
                level_scene_path = scene_path.replace("original", level)

                level_raster_data["scene_states"][scene_path]["draw_method"] = 2
                level_raster_data["settings"]["lighting"]["depth_threshold"] = 0.0

            level_raster_data["scene_states"][level_scene_path] = level_raster_data["scene_states"].pop(scene_path)
            level_raster_data["scene_states"][level_scene_path]["path"] = level_scene_path
            level_raster_data["scene_states"][level_scene_path]["name"] = model
            level_raster_data["scene_states"][level_scene_path]["fixed_lod"] = 16
            level_raster_data["scene_states"][level_scene_path]["octree_depth"] = 16
            level_raster_data["scene_states"][level_scene_path]["frustum_culling_level"] = 16
            level_raster_data["scene_states"][level_scene_path]["occlusion_culling_level"] = 16
            level_raster_data["current_scene_path"] = level_scene_path
            level_raster_data["settings"]["scenes_directory"] = ""

            raster_out = os.path.join(config_dir, "raster.json")
            with open(raster_out, 'w') as f:
                json.dump(level_raster_data, f, indent=4)

            blk_content = "{\n"
            blk_content += f'  scene:s = "{level_scene_path.replace("scom2_0.bin", "scene.xml")}"\n'
            blk_content += f'  output_file:s = "{os.path.join(config_dir, "rt_result.png")}"\n'
            blk_content += f'  stats_file:s = "{os.path.join(config_dir, "rt_result.json")}"\n'
            blk_content += f'  width:i = {width}\n'
            blk_content += f'  height:i = {height}\n'
            blk_content += f'  spp:i = 1\n'
            blk_content += f"  camera_pos:p3 = {litert_camera['camera_pos'][0]}, {litert_camera['camera_pos'][1]}, {litert_camera['camera_pos'][2]}\n"
            blk_content += f"  camera_target:p3 = {litert_camera['camera_target'][0]}, {litert_camera['camera_target'][1]}, {litert_camera['camera_target'][2]}\n"
            blk_content += f"  camera_up:p3 = {litert_camera['camera_up'][0]}, {litert_camera['camera_up'][1]}, {litert_camera['camera_up'][2]}\n"
            blk_content += f'  fovy:r = {fov_y}\n'
            blk_content += f'  mode:i = 2\n'
            blk_content += f'  background_color:p4 = 1.0, 1.0, 1.0, 1.0\n'
            blk_content += f"  light_dir:p3 = {lighting['light_pos'][0]}, {lighting['light_pos'][1]}, {lighting['light_pos'][2]}\n"
            blk_content += f'  light_color:p3 = 1.0, 1.0, 1.0\n'
            blk_content += f'  ambient_light_color:p3 = {ambient}, {ambient}, {ambient}\n'
            blk_content += f'  render_mode_raw:i = 18\n'
            blk_content += "}"

            rt_out = os.path.join(config_dir, "rt.blk")
            with open(rt_out, 'w') as f:
                f.write(blk_content)
            print(f"Generating config {model}:{level}...ok")

        print(f"Processed all levels for {model}")

if __name__ == "__main__":
    if len(sys.argv) < 2:
        print("Usage: python3 get_configs_from_original.py <base_dir>")
        sys.exit(1)
    generate_configs(sys.argv[1])
