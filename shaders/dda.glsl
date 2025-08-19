#version 450
#extension GL_EXT_shader_image_load_formatted : require
#extension GL_EXT_scalar_block_layout : require
#extension GL_EXT_buffer_reference : require

#define MAX_STEP 512

vec4 color_palette[14] = {
    {0.0, 0.0, 0.0, 1.0}, {0.5, 0.5, 0.5, 1.0}, {0.25, 0.25, 0, 1.0},
    {0.2, 0.8, 0.1, 1.0}, {0.2, 0.9, 0.1, 1.0}, {0.8, 0.8, 0.0, 1.0},
    {0.9, 0.9, 0.9, 1.0}, {0.8, 0.5, 0.0, 1.0}, {0.0, 0.2, 0.8, 1.0},
    {0.1, 0.4, 0.1, 1.0}, {0.3, 0.1, 0.0, 1.0}, {1.0, 1.0, 1.0, 1.0},
    {1.0, 0.2, 0.0, 1.0}, {1.0, 0.0, 1.0, 1.0}};

layout(set = 0, binding = 0) uniform image2D renderTarget;

layout(local_size_x = 32, local_size_y = 32, local_size_z = 1) in;

struct Chunk {
    int data[384][16][16];
};

layout(scalar, buffer_reference) buffer ChunkArray { Chunk chunks[]; };

layout(scalar, push_constant) uniform PC {
    ChunkArray chunk_array;
    uint       chunk_count;
    int        min_cx;
    int        min_cz;
    uint       grid_w;
    vec3       pos;
    mat4       r;
} pc;

int floorDiv(int a, int b) {
    // exact floor division for negatives
    int q = a / b;
    int r = a - q*b;
    return (r != 0 && ((r > 0) != (b > 0))) ? (q - 1) : q;
}

int modFloor(int a, int b) {
    int m = a - floorDiv(a,b)*b;
    return m;
}

bool mapToIndexAndLocal(ivec3 m, out uint idx, out ivec3 local) {
    // world block -> chunk coords
    int cx = floorDiv(m.x, 16);
    int cz = floorDiv(m.z, 16);

    // local coords inside chunk [0..15], y [0..383]
    int lx = modFloor(m.x, 16);
    int lz = modFloor(m.z, 16);
    int ly = m.y;

    if (ly < 0 || ly >= 384) return false;

    int gx = cx - pc.min_cx; // grid x
    int gz = cz - pc.min_cz; // grid z
    if (gx < 0 || gz < 0) return false;

    uint u_gx = uint(gx);
    uint u_gz = uint(gz);
    idx = u_gz * pc.grid_w + u_gx;
    if (idx >= pc.chunk_count) return false;

    local = ivec3(lx, ly, lz);
    return true;
}

int blockAt(ivec3 m) {
    uint idx;
    ivec3 l;
    if (!mapToIndexAndLocal(m, idx, l)) return 0;
    return pc.chunk_array.chunks[idx].data[l.y][l.x][l.z];
}

bool isBlock(ivec3 m) {
    return blockAt(m) > 0;
}

vec4 blockColor(ivec3 m) {
    int b = blockAt(m);
    vec4 c = vec4(0.0, 0.0, 0.0, 1.0);
    if (b >= 0 && b < 14) {
        c = color_palette[b];
    }
    return c;
}

void main() {
    ivec2 img_size = imageSize(renderTarget);
    float aspect_ratio = img_size.x / float(img_size.y);

    // start at world position
    ivec3 map = ivec3(floor(pc.pos));

    // normalized screen coordinates
    vec2 screen = gl_GlobalInvocationID.xy / vec2(img_size) * 2 - 1;
    screen.x *= aspect_ratio;
    screen.y = -screen.y;

    vec4 d = vec4(screen.x, screen.y, -1, 0);
    vec4 d_prime = normalize(pc.r * d);
    vec3 ray_dir = d_prime.xyz;

    vec3 deltaDist = abs(1.0 / ray_dir);
    ivec3 rayStep = ivec3(sign(ray_dir));
    vec3 sideDist = (sign(ray_dir) * (vec3(map) - pc.pos)
                   + (sign(ray_dir) * 0.5) + 0.5) * deltaDist;

    bool hit = false;
    bvec3 mask = bvec3(false, true, false);

    for (int i = 0; i < MAX_STEP; i++) {
        if (isBlock(map)) { hit = true; break; }

        if (sideDist.x < sideDist.y) {
            if (sideDist.x < sideDist.z) {
                sideDist.x += deltaDist.x; map.x += rayStep.x; mask = bvec3(true, false, false);
            } else {
                sideDist.z += deltaDist.z; map.z += rayStep.z; mask = bvec3(false, false, true);
            }
        } else {
            if (sideDist.y < sideDist.z) {
                sideDist.y += deltaDist.y; map.y += rayStep.y; mask = bvec3(false, true, false);
            } else {
                sideDist.z += deltaDist.z; map.z += rayStep.z; mask = bvec3(false, false, true);
            }
        }
    }

    vec4 color = vec4(0.0);
    if (hit) {
        if (mask.x) color = vec4(0.5);
        if (mask.y) color = vec4(1.0);
        if (mask.z) color = vec4(0.75);
        color *= blockColor(map);
    }

    imageStore(renderTarget, ivec2(gl_GlobalInvocationID.xy), color);
}
