#version 450
#extension GL_EXT_shader_image_load_formatted : require
#extension GL_EXT_scalar_block_layout : require
#extension GL_EXT_buffer_reference : require

#define MAX_STEP 100
#define CUNK_CHUNK_SIZE 16
#define CUNK_CHUNK_MAX_HEIGHT 384

layout(location = 0) out vec4 colorOut;

layout(scalar, buffer_reference) buffer VertexBuffer {
    vec3 vertices[36];
    vec3 vertexColors[36];
};
layout(scalar, buffer_reference) buffer ChunkBuffer {
    int block_data[CUNK_CHUNK_SIZE][CUNK_CHUNK_MAX_HEIGHT][CUNK_CHUNK_SIZE];
};

layout(scalar, push_constant) uniform T {
    VertexBuffer vertex_buffer;
    ChunkBuffer chunk_buffer;
    ivec3 chunk_position;
    mat4 matrix;
    mat4 inv_matrix;
    vec3 camera_pos;
}
push_constants;

vec4 color_palette[14] = {
    {0.0, 0.0, 0.0, 1.0}, {0.5, 0.5, 0.5, 1.0}, {0.25, 0.25, 0, 1.0},
    {0.2, 0.8, 0.1, 1.0}, {0.2, 0.9, 0.1, 1.0}, {0.8, 0.8, 0.0, 1.0},
    {0.9, 0.9, 0.9, 1.0}, {0.8, 0.5, 0.0, 1.0}, {0.0, 0.2, 0.8, 1.0},
    {0.1, 0.4, 0.1, 1.0}, {0.3, 0.1, 0.0, 1.0}, {1.0, 1.0, 1.0, 1.0},
    {1.0, 0.2, 0.0, 1.0}, {1.0, 0.0, 1.0, 1.0}};

bool isBlock(ivec3 m) {
    return m.x >= 0 && m.x < CUNK_CHUNK_SIZE && m.y >= 0 &&
           m.y < CUNK_CHUNK_MAX_HEIGHT && m.z >= 0 && m.z < CUNK_CHUNK_SIZE &&
           push_constants.chunk_buffer.block_data[m.x][m.y][m.z] > 0;
}

vec4 blockColor(ivec3 m) {
    vec4 c = vec4(0);
    int b = push_constants.chunk_buffer.block_data[m.x][m.y][m.z];
    if (b > 0 && b < 14) {
        c = color_palette[b];
    }
    return c;
}

void main() {
    vec3 pos = push_constants.camera_pos;

    // world to local chunk coord
    pos = pos - push_constants.chunk_position;

    ivec2 image_size_px = ivec2(1024, 1024);
    float aspect_ratio = image_size_px.x / float(image_size_px.y);

    vec2 pixel = gl_FragCoord.xy - vec2(0.5, 0.5);

    vec2 screen_uv = (2.0 * pixel / vec2(image_size_px)) - 1.0;
    screen_uv.x *= aspect_ratio;
    screen_uv.y = -screen_uv.y;
    vec4 ray_direction_camera = vec4(screen_uv.x, screen_uv.y, -1.0, 0.0);

    vec4 dir_aff = normalize(push_constants.inv_matrix * ray_direction_camera);
    vec3 dir = dir_aff.xyz;

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
            colorOut = color * blockColor(map);
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