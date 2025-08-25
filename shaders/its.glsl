// its.glsl (Intersection Shader)
// Calculate intersection of ray with the Axis Aligned Bounding Boxes (AABBs)
// that enclosed each chunk. Result is saved to be used in DDA Shader

#version 450
#extension GL_EXT_shader_image_load_formatted : require
#extension GL_EXT_scalar_block_layout : require
#extension GL_EXT_buffer_reference : require

#define EPSILON 1e-10
#define NUM_CHUNKS 3
#define CUNK_CHUNK_SIZE 16
#define CUNK_CHUNK_MAX_HEIGHT 384

layout(set = 0, binding = 0) uniform image2D renderTarget;

layout(local_size_x = 32, local_size_y = 32, local_size_z = 1) in;

layout(scalar, push_constant) uniform T {
    ivec2 chunk_indices[NUM_CHUNKS];
    vec3 pos;
    mat4 r;
}
push_constants;

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
        return vec4(1, 0, 0, 1);
    }
    if (chunk_idx.y == 0) {
        return vec4(0, 0, 1, 1);
    }
    return vec4(0);
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

    float t_closest = 1 / 0.0; // +inf
    int idx_closest = -1;
    vec4 c = vec4(0);
    for (int i = 0; i < NUM_CHUNKS; i++) {
        float t_aabb = 0;
        bool its = slab(push_constants.chunk_indices[i], push_constants.pos,
                        inv_ray_dir, t_aabb);

        if (its && t_aabb <= t_closest) {
            t_closest = t_aabb;
            idx_closest = i;
        }
    }

    if (idx_closest >= 0) {
        imageStore(renderTarget, ivec2(gl_GlobalInvocationID.xy),
                   aabb_debug_color_palette(
                       push_constants.chunk_indices[idx_closest]));
    }
}