#version 450
#extension GL_EXT_shader_image_load_formatted : require
#extension GL_EXT_scalar_block_layout : require
#extension GL_EXT_buffer_reference : require

#define MAX_STEP 100

vec4 color_palette[14] = {
    {0.0, 0.0, 0.0, 1.0}, {0.5, 0.5, 0.5, 1.0}, {0.25, 0.25, 0, 1.0},
    {0.2, 0.8, 0.1, 1.0}, {0.2, 0.9, 0.1, 1.0}, {0.8, 0.8, 0.0, 1.0},
    {0.9, 0.9, 0.9, 1.0}, {0.8, 0.5, 0.0, 1.0}, {0.0, 0.2, 0.8, 1.0},
    {0.1, 0.4, 0.1, 1.0}, {0.3, 0.1, 0.0, 1.0}, {1.0, 1.0, 1.0, 1.0},
    {1.0, 0.2, 0.0, 1.0}, {1.0, 0.0, 1.0, 1.0}};

layout(set = 0, binding = 0) uniform image2D renderTarget;

layout(local_size_x = 32, local_size_y = 32, local_size_z = 1) in;

layout(scalar, buffer_reference) buffer ChuckBuffer { int data[384][16][16]; };

layout(scalar, push_constant) uniform T {
    ChuckBuffer chuck_buffer;
    vec3 pos;
    mat4 r;
}
push_constants;

bool isBlock(ivec3 m) {
    return m.x >= 0 && m.x < 16 && m.z >= 0 && m.z < 16 && m.y >= 0 &&
           m.y < 384 && push_constants.chuck_buffer.data[m.y][m.x][m.z] > 0;
}

vec4 blockColor(ivec3 m) {
    int b = push_constants.chuck_buffer.data[m.y][m.x][m.z];
    vec4 c = vec4(0.0, 0.0, 0.0, 1.0);
    if (b >= 0 && b < 14)
        c = color_palette[b];

    return c;
}

void main() {
    ivec2 img_size = imageSize(renderTarget);

    // block on map
    ivec3 map = ivec3(floor(push_constants.pos));

    // normalized screen coordinates
    vec2 screen = gl_GlobalInvocationID.xy / vec2(img_size) * 2 - 1;
    screen.y = -screen.y;

    vec4 d = vec4(screen.x, screen.y, -1, 0);
    vec4 d_prime = normalize(push_constants.r * d);

    vec3 ray_dir = d_prime.xyz;

    vec3 deltaDist = abs(1 / ray_dir);

    ivec3 rayStep = ivec3(sign(ray_dir));
    vec3 sideDist = (sign(ray_dir) * (vec3(map) - push_constants.pos) +
                     (sign(ray_dir) * 0.5) + 0.5) *
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