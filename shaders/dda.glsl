#version 450
#extension GL_EXT_shader_image_load_formatted : require
#extension GL_EXT_scalar_block_layout : require
#extension GL_EXT_buffer_reference : require

#define MAX_RAY_STEPS 512

layout(set = 0, binding = 0) uniform image2D output_image;
layout(local_size_x = 32, local_size_y = 32, local_size_z = 1) in;

struct Chunk {
    int blocks[384][16][16];
};

layout(scalar, buffer_reference) buffer ChunkBuffer {
    Chunk chunks[];
};

layout(scalar, push_constant) uniform PushConstants {
    ChunkBuffer chunk_buffer;
    int         chunk_count;
    int         render_radius;
    ivec2       chunk_center;
    vec3        camera_position;
    mat4        camera_rotation;
} pc;

bool get_chunk_index_and_local_coords(ivec3 world_block_pos, out uint chunk_index, out ivec3 local_coords) {
    int min_cx = pc.chunk_center.x - pc.render_radius;
    int min_cz = pc.chunk_center.y - pc.render_radius;
    
    int chunk_x = int(floor(float(world_block_pos.x) / 16.0));
    int chunk_z = int(floor(float(world_block_pos.z) / 16.0));

    // If block is minimum within radius, 0,0 and max is 2*r + 1 on x and z
    int grid_x = chunk_x - min_cx;
    int grid_z = chunk_z - min_cz;

    if (grid_x < 0 || grid_z < 0 || grid_x >= (2 * pc.render_radius + 1) || grid_z >= (2 * pc.render_radius + 1))
        return false;

    chunk_index = uint(grid_z * (2 * pc.render_radius + 1) + grid_x);
    if (chunk_index >= pc.chunk_count)
        return false;

    int local_x = int(mod(float(world_block_pos.x), 16.0));
    int local_z = int(mod(float(world_block_pos.z), 16.0));
    int local_y = world_block_pos.y;

    if (local_y < 0 || local_y >= 384)
        return false;

    local_coords = ivec3(local_x, local_y, local_z);
    return true;
}

int get_block_at(ivec3 world_block_pos) {
    uint chunk_index;
    ivec3 local_coords;
    if (!get_chunk_index_and_local_coords(world_block_pos, chunk_index, local_coords))
        return 0;
    return pc.chunk_buffer.chunks[chunk_index].blocks[local_coords.y][local_coords.x][local_coords.z];
}

// true if block is solid
bool is_solid_block(ivec3 world_block_pos) {
    return get_block_at(world_block_pos) > 0;
}

vec4 get_block_color(ivec3 world_block_pos) {
    int block_id = get_block_at(world_block_pos);
    vec4 color = vec4(0.0, 0.0, 0.0, 1.0);

    if (block_id >= 0 && block_id < 14) {
        const vec4 block_palette[14] = vec4[14](
            vec4(0.0, 0.0, 0.0, 1.0),  vec4(0.5, 0.5, 0.5, 1.0),
            vec4(0.25,0.25,0.0,1.0),   vec4(0.2, 0.8, 0.1, 1.0),
            vec4(0.2, 0.9, 0.1, 1.0),  vec4(0.8, 0.8, 0.0, 1.0),
            vec4(0.9, 0.9, 0.9, 1.0),  vec4(0.8, 0.5, 0.0, 1.0),
            vec4(0.0, 0.2, 0.8, 1.0),  vec4(0.1, 0.4, 0.1, 1.0),
            vec4(0.3, 0.1, 0.0, 1.0),  vec4(1.0, 1.0, 1.0, 1.0),
            vec4(1.0, 0.2, 0.0, 1.0),  vec4(1.0, 0.0, 1.0, 1.0)
        );
        color = block_palette[block_id];
    }
    return color;
}

void main() {
    ivec2 image_size_px = imageSize(output_image);
    float aspect_ratio = image_size_px.x / float(image_size_px.y);

    // start at world voxel containing camera
    ivec3 current_block = ivec3(floor(pc.camera_position));

    // do the ndc and get ray
    vec2 screen_uv = (2.0 * gl_GlobalInvocationID.xy / vec2(image_size_px)) - 1.0;
    screen_uv.x *= aspect_ratio;
    screen_uv.y = -screen_uv.y;

    vec4 ray_direction_clip = vec4(screen_uv.x, screen_uv.y, -1.0, 0.0);
    vec4 ray_direction_world = normalize(pc.camera_rotation * ray_direction_clip);
    vec3 ray_dir = ray_direction_world.xyz;

    // DDA setup
    vec3 delta_distance = abs(1.0 / ray_dir);
    ivec3 step_direction = ivec3(sign(ray_dir));
    vec3 side_distance = (sign(ray_dir) * (vec3(current_block) - pc.camera_position) + (sign(ray_dir) * 0.5) + 0.5) * delta_distance;

    bool hit = false;
    bvec3 hit_normal_mask = bvec3(false, true, false);

    // march through voxels
    for (int step = 0; step < MAX_RAY_STEPS; step++) {
        if (is_solid_block(current_block)) {
            hit = true;
            break;
        }

        if (side_distance.x < side_distance.y) {
            if (side_distance.x < side_distance.z) {
                side_distance.x += delta_distance.x;
                current_block.x += step_direction.x;
                hit_normal_mask = bvec3(true, false, false);
            } else {
                side_distance.z += delta_distance.z;
                current_block.z += step_direction.z;
                hit_normal_mask = bvec3(false, false, true);
            }
        } else {
            if (side_distance.y < side_distance.z) {
                side_distance.y += delta_distance.y;
                current_block.y += step_direction.y;
                hit_normal_mask = bvec3(false, true, false);
            } else {
                side_distance.z += delta_distance.z;
                current_block.z += step_direction.z;
                hit_normal_mask = bvec3(false, false, true);
            }
        }
    }

    vec4 final_color = vec4(0.0);
    if (hit) {
        if (hit_normal_mask.x) final_color = vec4(0.5);
        if (hit_normal_mask.y) final_color = vec4(1.0);
        if (hit_normal_mask.z) final_color = vec4(0.75);
        final_color *= get_block_color(current_block);
    }

    imageStore(output_image, ivec2(gl_GlobalInvocationID.xy), final_color);
}
