// its.glsl (Intersection Shader)
// Calculate intersection of ray with the Axis Aligned Bounding Boxes (AABBs)
// that enclosed each chunk. Result is saved to be used in DDA Shader

#version 450
#extension GL_EXT_shader_image_load_formatted : require
#extension GL_EXT_scalar_block_layout : require
#extension GL_EXT_buffer_reference : require

#define MAX_STEP 100
#define EPSILON 1e-10
#define NUM_CHUNKS 1089
#define CUNK_CHUNK_SIZE 16
#define CUNK_CHUNK_MAX_HEIGHT 384

layout(set = 0, binding = 0) uniform image2D renderTarget;

layout(local_size_x = 32, local_size_y = 32, local_size_z = 1) in;

layout(scalar, buffer_reference) buffer ChunkIndicesBuffer {
    ivec2 indices[NUM_CHUNKS];
};

struct Chunk {
    int blocks[CUNK_CHUNK_SIZE][CUNK_CHUNK_MAX_HEIGHT][CUNK_CHUNK_SIZE];
};

layout(scalar, buffer_reference) buffer ChunkBuffer {
    Chunk chunks[NUM_CHUNKS];
};

layout(scalar, push_constant) uniform T {
    ChunkIndicesBuffer chunk_indicies_buffer;
    ChunkBuffer chunk_buffer;
    vec3 pos;
    mat4 r;
}
push_constants;

struct ChunkIntersection {
    float t;
    bool its;
    int i;
};

vec4 color_palette[14] = {
    {0.0, 0.0, 0.0, 1.0}, {0.5, 0.5, 0.5, 1.0}, {0.25, 0.25, 0, 1.0},
    {0.2, 0.8, 0.1, 1.0}, {0.2, 0.9, 0.1, 1.0}, {0.8, 0.8, 0.0, 1.0},
    {0.9, 0.9, 0.9, 1.0}, {0.8, 0.5, 0.0, 1.0}, {0.0, 0.2, 0.8, 1.0},
    {0.1, 0.4, 0.1, 1.0}, {0.3, 0.1, 0.0, 1.0}, {1.0, 1.0, 1.0, 1.0},
    {1.0, 0.2, 0.0, 1.0}, {1.0, 0.0, 1.0, 1.0}};

// Calculate intersection of an AABB
bool slab(ivec2 c_idx, vec3 o, vec3 inv_d, out float t) {
    // Point "a" is the bounding box minimum and "b" is the maximum.
    vec3 a = vec3(c_idx.x * CUNK_CHUNK_SIZE, 0, c_idx.y * CUNK_CHUNK_SIZE);
    vec3 b = vec3(a.x + CUNK_CHUNK_SIZE, CUNK_CHUNK_MAX_HEIGHT,
                  a.z + CUNK_CHUNK_SIZE);

    // t_a stores the 3 intersections of the ray with the 3 hyperplanes defined
    // by the 3 normals (the 3 basis vectors) that go through point "a" . Think
    // of the 3 planes of the cube spanned at the lower-left (or the min)
    // corner. Similar story for t_b.
    vec3 t_a = (a - o) * inv_d;
    vec3 t_b = (b - o) * inv_d;

    vec3 t_min = min(t_a, t_b);
    vec3 t_max = max(t_a, t_b);

    float max_t_min = max(t_min.x, max(t_min.y, t_min.z));
    float min_t_max = min(t_max.x, min(t_max.y, t_max.z));

    bool its = min_t_max >= EPSILON && min_t_max > max_t_min;

    if (its) {
        t = min_t_max;
        // we want the clostest intersection (smallest positive t)
        if (max_t_min >= EPSILON)
            t = max_t_min;
    }

    return its;
}

vec4 aabb_debug_color_palette(ivec2 chunk_idx) {
    if (chunk_idx.x == 0) {
        return vec4(1, mod(chunk_idx.y, 3), 0, 1);
    }
    if (chunk_idx.y == 0) {
        return vec4(0, 0, 1, 1);
    }
    return vec4(0);
}

bool isBlock(int ci, ivec3 m) {
    return m.x >= 0 && m.x < CUNK_CHUNK_SIZE && m.y >= 0 &&
           m.y < CUNK_CHUNK_MAX_HEIGHT && m.z >= 0 && m.z < CUNK_CHUNK_SIZE &&
           push_constants.chunk_buffer.chunks[ci].blocks[m.x][m.y][m.z] > 0;
}

vec4 blockColor(int ci, ivec3 m) {
    vec4 c = vec4(0);
    int b = push_constants.chunk_buffer.chunks[ci].blocks[m.x][m.y][m.z];
    if (b > 0 && b < 14) {
        c = color_palette[b];
    }
    return c;
}

// pos and dir are in local chunk's space
bool dda(vec3 pos, vec3 dir, int ci, out vec4 color_out) {
    // block indices on map
    ivec3 map = ivec3(floor(pos));

    vec3 deltaDist = abs(1 / dir);

    ivec3 rayStep = ivec3(sign(dir));
    vec3 sideDist =
        (sign(dir) * (vec3(map) - pos) + (sign(dir) * 0.5) + 0.5) * deltaDist;

    bvec3 mask = bvec3(false, true, false);

    // perform DDA
    for (int i = 0; i < MAX_STEP; i++) {

        // if hit block
        if (isBlock(ci, map)) {
            vec4 color;
            // fake shadow on sides
            if (mask.x) {
                color = vec4(0.5);
            }
            if (mask.y) {
                color = vec4(1.0);
            }
            if (mask.z) {
                color = vec4(0.75);
            }

            // mix shadow color with block color
            color_out = color * blockColor(ci, map);
            return true;
        }

        if (sideDist.x < sideDist.y) {
            if (sideDist.x < sideDist.z) {
                sideDist.x += deltaDist.x;
                map.x += rayStep.x;
                mask = bvec3(true, false, false);
            } else {
                sideDist.z += deltaDist.z;
                map.z += rayStep.z;
                mask = bvec3(false, false, true);
            }
        } else {
            if (sideDist.y < sideDist.z) {
                sideDist.y += deltaDist.y;
                map.y += rayStep.y;
                mask = bvec3(false, true, false);
            } else {
                sideDist.z += deltaDist.z;
                map.z += rayStep.z;
                mask = bvec3(false, false, true);
            }
        }
    }

    return false;
}

void main() {
    ivec2 img_size = imageSize(renderTarget);
    float aspect_ratio = img_size.x / float(img_size.y);

    // normalized screen coordinates
    vec2 screen = gl_GlobalInvocationID.xy / vec2(img_size) * 2 - 1;
    screen.x = screen.x * aspect_ratio;
    screen.y = -screen.y;

    vec4 d = vec4(screen.x, screen.y, -1, 0);

    vec4 d_prime = normalize(push_constants.r * d);

    vec3 ray_dir = d_prime.xyz;

    vec3 inv_ray_dir = 1 / ray_dir;

    // collision test
    ChunkIntersection isecs[NUM_CHUNKS];
    for (int i = 0; i < NUM_CHUNKS; i++) {
        float t_aabb = 1 / 0.0;
        bool its = slab(push_constants.chunk_indicies_buffer.indices[i],
                        push_constants.pos, inv_ray_dir, t_aabb);
        isecs[i] = ChunkIntersection(t_aabb, its, i);
    }

    // sort collision test result
    bool swapped;
    for (int i = 0; i < NUM_CHUNKS - 1; i++) {
        swapped = false;
        for (int j = 0; j < NUM_CHUNKS - i - 1; j++) {
            if (isecs[j].t > isecs[j + 1].t) {
                ChunkIntersection tmp = isecs[j];
                isecs[j] = isecs[j + 1];
                isecs[j + 1] = tmp;
                swapped = true;
            }
        }

        // If no two elements were swapped, then break
        if (!swapped)
            break;
    }

    // draw with dda
    for (int i = 0; i < NUM_CHUNKS; i++) {
        if (isecs[i].its) {
            vec3 pos = push_constants.pos;
            ivec2 chunk_idx =
                push_constants.chunk_indicies_buffer.indices[isecs[i].i];

            // project pos to chunk (intersection) surface
            pos = pos + ray_dir * (isecs[i].t + 1e-4);

            // world to chunk/object space
            pos = pos + vec3(-chunk_idx.x * CUNK_CHUNK_SIZE, 0,
                             -chunk_idx.y * CUNK_CHUNK_SIZE);

            vec4 c = vec4(0);
            bool dda_its = dda(pos, ray_dir, isecs[i].i, c);
            if (dda_its) {
                imageStore(renderTarget, ivec2(gl_GlobalInvocationID.xy), c);
                break;
            }
        }
    }

    // if (idx_closest >= 0) {

    //     // Ray-aabb intersection debug

    //     // imageStore(renderTarget, ivec2(gl_GlobalInvocationID.xy),
    //     //            aabb_debug_color_palette(chunk_index_closest));

    //     // Previously push_constants is accessed non-uniformly that caused
    //     // z-fighting (different idx_closest)

    //     // imageStore(renderTarget, ivec2(gl_GlobalInvocationID.xy),
    //     //            aabb_debug_color_palette(
    //     //                push_constants.chunk_indices[idx_closest]));
    // }
}