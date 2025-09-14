#version 450
#extension GL_EXT_shader_image_load_formatted : require
#extension GL_EXT_scalar_block_layout : require
#extension GL_EXT_buffer_reference : require

#define MAX_RAY_STEPS 256

layout(set = 0, binding = 0) uniform image2D output_image;
layout(local_size_x = 32, local_size_y = 32, local_size_z = 1) in;

struct Chunk {
    ivec2 absolute_pos;
    int blocks[384][16][16];
};

layout(scalar, buffer_reference) buffer ChunkBuffer { Chunk chunks[]; };

layout(scalar, push_constant) uniform PushConstants {
    ChunkBuffer chunk_buffer;
    ivec2 chunk_center;
    int render_radius;
    vec3 camera_position;
    mat4 camera_rotation;
}
pc;

bool get_chunk_index_and_local_coords(ivec3 world_block_pos,
                                      out uint chunk_index,
                                      out ivec3 local_coords) {

    int grid_size = 2 * pc.render_radius + 1;

    int chunk_x = int(floor(float(world_block_pos.x) / 16.0));
    int chunk_z = int(floor(float(world_block_pos.z) / 16.0));

    int sx = int(mod(chunk_x, grid_size));
    int sz = int(mod(chunk_z, grid_size));
    chunk_index = uint(sz * grid_size + sx);

    ivec2 tag = ivec2(pc.chunk_buffer.chunks[chunk_index].absolute_pos);
    if (tag.x != chunk_x || tag.y != chunk_z)
        return false;

    int local_x = world_block_pos.x - chunk_x * 16;
    int local_z = world_block_pos.z - chunk_z * 16;
    int local_y = world_block_pos.y;

    if (local_y < 0 || local_y >= 384)
        return false;

    local_coords = ivec3(local_x, local_y, local_z);
    return true;
}

int get_block_at(ivec3 world_block_pos) {
    uint chunk_index;
    ivec3 local_coords;
    if (!get_chunk_index_and_local_coords(world_block_pos, chunk_index,
                                          local_coords))
        return 0;
    return pc.chunk_buffer.chunks[chunk_index]
        .blocks[local_coords.y][local_coords.x][local_coords.z];
}

bool is_solid_block(ivec3 world_block_pos) {
    return get_block_at(world_block_pos) > 0;
}

vec4 get_block_color(ivec3 world_block_pos) {
    int block_id = get_block_at(world_block_pos);
    vec4 color = vec4(0.0, 0.0, 0.0, 1.0);

    if (block_id >= 0 && block_id < 14) {
        const vec4 block_palette[14] =
            vec4[14](vec4(0.0, 0.0, 0.0, 1.0), vec4(0.5, 0.5, 0.5, 1.0),
                     vec4(0.25, 0.25, 0.0, 1.0), vec4(0.2, 0.8, 0.1, 1.0),
                     vec4(0.2, 0.9, 0.1, 1.0), vec4(0.8, 0.8, 0.0, 1.0),
                     vec4(0.9, 0.9, 0.9, 1.0), vec4(0.8, 0.5, 0.0, 1.0),
                     vec4(0.0, 0.2, 0.8, 1.0), vec4(0.1, 0.4, 0.1, 1.0),
                     vec4(0.3, 0.1, 0.0, 1.0), vec4(1.0, 1.0, 1.0, 1.0),
                     vec4(1.0, 0.2, 0.0, 1.0), vec4(1.0, 0.0, 1.0, 1.0));
        color = block_palette[block_id];
    }
    return color;
}

void main() {
    ivec2 image_size_px = imageSize(output_image);
    float aspect_ratio = image_size_px.x / float(image_size_px.y);

    vec2 screen_uv =
        (2.0 * gl_GlobalInvocationID.xy / vec2(image_size_px)) - 1.0;
    screen_uv.x *= aspect_ratio;
    screen_uv.y = -screen_uv.y;

    vec4 ray_direction_clip = vec4(screen_uv.x, screen_uv.y, -1.0, 0.0);
    vec4 ray_direction_world =
        normalize(pc.camera_rotation * ray_direction_clip);
    vec3 ray_dir = ray_direction_world.xyz;

    ivec3 current_block = ivec3(floor(pc.camera_position));

    vec3 delta_distance = abs(1.0 / ray_dir);
    ivec3 step_direction = ivec3(sign(ray_dir));
    vec3 side_distance =
        (sign(ray_dir) * (vec3(current_block) - pc.camera_position) +
         (sign(ray_dir) * 0.5) + 0.5) *
        delta_distance;

    bool hit = false;
    bvec3 hit_normal_mask = bvec3(false, true, false);

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
        if (hit_normal_mask.x)
            final_color = vec4(0.5);
        if (hit_normal_mask.y)
            final_color = vec4(1.0);
        if (hit_normal_mask.z)
            final_color = vec4(0.75);
        final_color *= get_block_color(current_block);
    }

    imageStore(output_image, ivec2(gl_GlobalInvocationID.xy), final_color);
}
