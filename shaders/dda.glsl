// dda.glsl (DDA Shader)
// Calculate hit block given chunk map and ray in local (chunk) coordinates

#version 450
#extension GL_EXT_shader_image_load_formatted : require
#extension GL_EXT_scalar_block_layout : require
#extension GL_EXT_buffer_reference : require

#define MAX_STEP 100
#define CUNK_CHUNK_SIZE 16
#define CUNK_CHUNK_MAX_HEIGHT 384

vec4 color_palette[14] = {
    {0.0, 0.0, 0.0, 1.0}, {0.5, 0.5, 0.5, 1.0}, {0.25, 0.25, 0, 1.0},
    {0.2, 0.8, 0.1, 1.0}, {0.2, 0.9, 0.1, 1.0}, {0.8, 0.8, 0.0, 1.0},
    {0.9, 0.9, 0.9, 1.0}, {0.8, 0.5, 0.0, 1.0}, {0.0, 0.2, 0.8, 1.0},
    {0.1, 0.4, 0.1, 1.0}, {0.3, 0.1, 0.0, 1.0}, {1.0, 1.0, 1.0, 1.0},
    {1.0, 0.2, 0.0, 1.0}, {1.0, 0.0, 1.0, 1.0}};

layout(set = 0, binding = 0) uniform image2D renderTarget;

layout(local_size_x = 32, local_size_y = 32, local_size_z = 1) in;

layout(scalar, buffer_reference) buffer ChuckBuffer {
    int data[CUNK_CHUNK_SIZE][CUNK_CHUNK_MAX_HEIGHT][CUNK_CHUNK_SIZE];
};

layout(scalar, push_constant) uniform T {
    ChuckBuffer chunk_buffer;
    vec3 pos;
    vec3 dir;
}
push_constants;

bool isBlock(ivec3 m) {
    return m.x >= 0 && m.x < CUNK_CHUNK_SIZE && m.y >= 0 &&
           m.y < CUNK_CHUNK_MAX_HEIGHT && m.z >= 0 && m.z < CUNK_CHUNK_SIZE &&
           push_constants.chunk_buffer.data[m.x][m.y][m.z] > 0;
}

vec4 blockColor(ivec3 m) {
    vec4 c = vec4(0);
    int b = push_constants.chunk_buffer.data[m.x][m.y][m.z];
    if (b > 0 && b < 14) {
        c = color_palette[b];
    }
    return c;
}

void main() {
    // block indices on map
    ivec3 map = ivec3(floor(push_constants.pos));

    vec3 deltaDist = abs(1 / push_constants.dir);

    ivec3 rayStep = ivec3(sign(push_constants.dir));
    vec3 sideDist =
        (sign(push_constants.dir) * (vec3(map) - push_constants.pos) +
         (sign(push_constants.dir) * 0.5) + 0.5) *
        deltaDist;

    bvec3 mask = bvec3(false, true, false);

    // perform DDA
    for (int i = 0; i < MAX_STEP; i++) {

        // if hit block
        if (isBlock(map)) {
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
            color = color * blockColor(map);
            // paint color on screen
            imageStore(renderTarget, ivec2(gl_GlobalInvocationID.xy), color);
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
}