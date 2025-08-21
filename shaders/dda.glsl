#version 450
#extension GL_EXT_shader_image_load_formatted : require
#extension GL_EXT_scalar_block_layout : require
#extension GL_EXT_buffer_reference : require

// ---------- CONFIG ----------
#define MAX_STEP 512

// ---------- OUTPUT ----------
layout(set = 0, binding = 0) uniform image2D renderTarget;
layout(local_size_x = 32, local_size_y = 32, local_size_z = 1) in;

struct Chunk {
    int data[384][16][16];
};

layout(scalar, buffer_reference) buffer ChunkArray {
    Chunk chunks[];
};

// ---------- PUSH CONSTANTS ----------
layout(scalar, push_constant) uniform PC {
    ChunkArray chunk_array; // device-addressable base pointer to first Chunk
    int        chunk_count; // grid_size * grid_size
    int        grid_size;   // 2*radius + 1 (must be odd)
    vec3       pos;         // camera/world position (float)
    mat4       r;           // camera-to-world rotation (for ray dir)
} pc;

// ---------- UTILITIES ----------
int floorDiv(int a, int b) {
    // exact floor division for negative coordinates
    int q = a / b;
    int r = a - q * b;
    return (r != 0 && ((r > 0) != (b > 0))) ? (q - 1) : q;
}

int modFloor(int a, int b) {
    int m = a - floorDiv(a, b) * b;
    return m;
}

// Compute radius and min chunk from pc.pos and grid_size so host doesn't need to pass them
void computeGridOrigin(out int center_cx, out int center_cz, out int radius, out int min_cx, out int min_cz) {
    // center chunk at camera position
    center_cx = floorDiv(int(floor(pc.pos.x)), 16);
    center_cz = floorDiv(int(floor(pc.pos.z)), 16);
    radius    = int(pc.grid_size) / 2;            // grid_size is odd (e.g., 3 -> radius 1)
    min_cx    = center_cx - radius;
    min_cz    = center_cz - radius;
}

// Map a world voxel to (chunk index in compact grid, local voxel coords in that chunk)
bool mapToIndexAndLocal(ivec3 m, out uint idx, out ivec3 local) {
    int center_cx, center_cz, radius, min_cx, min_cz;
    computeGridOrigin(center_cx, center_cz, radius, min_cx, min_cz);

    // Which world chunk contains this voxel?
    int cx = floorDiv(m.x, 16);
    int cz = floorDiv(m.z, 16);

    // Position in the compact grid [0 .. grid_size-1]
    int gx = cx - min_cx;
    int gz = cz - min_cz;

    if (gx < 0 || gz < 0 || gx >= int(pc.grid_size) || gz >= int(pc.grid_size))
        return false;

    uint u_gx = uint(gx);
    uint u_gz = uint(gz);

    idx = u_gz * pc.grid_size + u_gx;
    if (idx >= pc.chunk_count) // safety
        return false;

    // Local coords inside chunk
    int lx = modFloor(m.x, 16);
    int lz = modFloor(m.z, 16);
    int ly = m.y;

    if (ly < 0 || ly >= 384)  // outside vertical bounds of the chunk stack
        return false;

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

// ---------- SIMPLE PALETTE ----------
vec4 blockColor(ivec3 m) {
    int b = blockAt(m);
    vec4 c = vec4(0.0, 0.0, 0.0, 1.0);

    if (b >= 0 && b < 14) {
        const vec4 color_palette[14] = vec4[14](
            vec4(0.0, 0.0, 0.0, 1.0), vec4(0.5, 0.5, 0.5, 1.0),
            vec4(0.25,0.25,0.0,1.0),  vec4(0.2, 0.8, 0.1, 1.0),
            vec4(0.2, 0.9, 0.1, 1.0), vec4(0.8, 0.8, 0.0, 1.0),
            vec4(0.9, 0.9, 0.9, 1.0), vec4(0.8, 0.5, 0.0, 1.0),
            vec4(0.0, 0.2, 0.8, 1.0), vec4(0.1, 0.4, 0.1, 1.0),
            vec4(0.3, 0.1, 0.0, 1.0), vec4(1.0, 1.0, 1.0, 1.0),
            vec4(1.0, 0.2, 0.0, 1.0), vec4(1.0, 0.0, 1.0, 1.0)
        );
        c = color_palette[b];
    }
    return c;
}

// ---------- MAIN ----------
void main() {
    ivec2 img_size = imageSize(renderTarget);
    float aspect_ratio = img_size.x / float(img_size.y);

    // Start at world voxel containing camera
    ivec3 map = ivec3(floor(pc.pos));

    // Normalized screen coords
    vec2 screen = gl_GlobalInvocationID.xy / vec2(img_size) * 2.0 - 1.0;
    screen.x *= aspect_ratio;
    screen.y = -screen.y;

    // Ray in world space
    vec4 d = vec4(screen.x, screen.y, -1.0, 0.0);
    vec4 d_prime = normalize(pc.r * d);
    vec3 ray_dir = d_prime.xyz;

    // DDA setup
    vec3 deltaDist = abs(1.0 / ray_dir);
    ivec3 rayStep = ivec3(sign(ray_dir));
    vec3 sideDist = (sign(ray_dir) * (vec3(map) - pc.pos) + (sign(ray_dir) * 0.5) + 0.5) * deltaDist;

    bool hit = false;
    bvec3 mask = bvec3(false, true, false);

    // Walk voxels
    for (int i = 0; i < MAX_STEP; i++) {
        if (isBlock(map)) {
            hit = true;
            break;
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

    vec4 color = vec4(0.0);
    if (hit) {
        if (mask.x) color = vec4(0.5);
        if (mask.y) color = vec4(1.0);
        if (mask.z) color = vec4(0.75);
        color *= blockColor(map);
    }

    imageStore(renderTarget, ivec2(gl_GlobalInvocationID.xy), color);
}
