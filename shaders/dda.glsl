#version 450
#extension GL_EXT_shader_image_load_formatted : require
#extension GL_EXT_scalar_block_layout : require
#extension GL_EXT_buffer_reference : require

#define MAX_STEP 100
#define NUM_CHUNKS_PER_AXIS 5

vec4 color_palette[14] = {
    {0.0, 0.0, 0.0, 1.0}, {0.5, 0.5, 0.5, 1.0}, {0.25, 0.25, 0, 1.0},
    {0.2, 0.8, 0.1, 1.0}, {0.2, 0.9, 0.1, 1.0}, {0.8, 0.8, 0.0, 1.0},
    {0.9, 0.9, 0.9, 1.0}, {0.8, 0.5, 0.0, 1.0}, {0.0, 0.2, 0.8, 1.0},
    {0.1, 0.4, 0.1, 1.0}, {0.3, 0.1, 0.0, 1.0}, {1.0, 1.0, 1.0, 1.0},
    {1.0, 0.2, 0.0, 1.0}, {1.0, 0.0, 1.0, 1.0}};

layout(set = 0, binding = 0) uniform image2D renderTarget;

layout(local_size_x = 32, local_size_y = 32, local_size_z = 1) in;

layout(scalar, buffer_reference) buffer ChuckBuffer {
    int data[NUM_CHUNKS_PER_AXIS][NUM_CHUNKS_PER_AXIS][384][16][16];
};

layout(scalar, push_constant) uniform T {
    ChuckBuffer chuck_buffer;
    ivec2 chunk_pos; // (cx, cz)
    vec3 pos;
    mat4 r;
}
push_constants;

bool isBlock(ivec3 m) {
    int cx = m.x / 16;
    int x = m.x - cx * 16;
    int cz = m.z / 16;
    int z = m.z - cz * 16;

    return cx >= 0 && cx < NUM_CHUNKS_PER_AXIS && cz >= 0 &&
           cz < NUM_CHUNKS_PER_AXIS && x >= 0 && x < 16 && z >= 0 && z < 16 &&
           m.y >= 0 && m.y < 384 &&
           push_constants.chuck_buffer.data[cx][cz][m.y][x][z] > 0;
}

vec4 blockColor(ivec3 m) {
    int cx = m.x / 16;
    int x = m.x - cx * 16;
    int cz = m.z / 16;
    int z = m.z - cz * 16;

    int b = push_constants.chuck_buffer.data[cx][cz][m.y][x][z];
    vec4 c = vec4(0.0, 0.0, 0.0, 1.0);
    if (b >= 0 && b < 14)
        c = color_palette[b];

    return c;
}

void main() {
    ivec2 img_size = imageSize(renderTarget);
    float aspect_ratio = img_size.x / float(img_size.y);
    vec3 pos = push_constants.pos - vec3(push_constants.chunk_pos.x * 16, 0,
                                         push_constants.chunk_pos.y * 16);
    // block on map
    ivec3 map = ivec3(floor(pos));

    // normalized screen coordinates
    vec2 screen = gl_GlobalInvocationID.xy / vec2(img_size) * 2 - 1;
    screen.x = screen.x * aspect_ratio;
    screen.y = -screen.y;

    vec4 d = vec4(screen.x, screen.y, -1, 0);
    vec4 d_prime = normalize(push_constants.r * d);

    vec3 ray_dir = d_prime.xyz;

    vec3 deltaDist = abs(1 / ray_dir);

    ivec3 rayStep = ivec3(sign(ray_dir));
    vec3 sideDist =
        (sign(ray_dir) * (vec3(map) - pos) + (sign(ray_dir) * 0.5) + 0.5) *
        deltaDist;

    bool hit = false;
    bvec3 mask = bvec3(false, true, false);

    vec4 c = vec4(0.0, 0.0, 0.0, 1.0);

    // perform DDA
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
        if (mask.x) {
            color = vec4(0.5);
        }
        if (mask.y) {
            color = vec4(1.0);
        }
        if (mask.z) {
            color = vec4(0.75);
        }
        color = color * blockColor(map);
    }

    imageStore(renderTarget, ivec2(gl_GlobalInvocationID.xy), color);
}