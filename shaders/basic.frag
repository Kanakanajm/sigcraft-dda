#version 450
#extension GL_EXT_shader_image_load_formatted : require
#extension GL_EXT_scalar_block_layout : require
#extension GL_EXT_buffer_reference : require

#define MAX_STEP 100 // visibility
#define CUNK_CHUNK_SIZE 16
#define CUNK_CHUNK_MAX_HEIGHT 384
#define EPSILON 1e-3
#define EPSILON_FACE 1e-3
#define EPSILON_CLAMP 1e-4

layout(location = 0)
in vec3 color;

layout(location = 0)
out vec4 colorOut;

layout(scalar, buffer_reference) buffer VertexBuffer {
    vec3 vertices[36];
    vec3 vertexColors[36];
};

layout(scalar, buffer_reference) buffer DebugBuffer {
    vec4 vectors[400*400];
};

layout(scalar, buffer_reference) buffer TransformBuffer {
    mat4 mpp; // perspective projection matrix
    mat4 m_cs_ws; // camera space to world space
    mat4 m_cs_ws_rot; // camera space to world space, rotation only
    vec3 camera_pos;
};

layout(scalar, buffer_reference) buffer BlockBuffer {
    uint blocks[CUNK_CHUNK_SIZE][CUNK_CHUNK_MAX_HEIGHT][CUNK_CHUNK_SIZE];
};

layout(scalar, push_constant) uniform T {
    VertexBuffer vertex_buffer;
    DebugBuffer debug_buffer;
    DebugBuffer debug2_buffer;
    TransformBuffer trans_buffer;
    BlockBuffer block_buffer;
    ivec4 chunk; // (x, y, z) position and id as w
    bool inChunk;
} push_constants;

vec4 color_palette[14] = {
    {0.0, 0.0, 0.0, 1.0}, {0.5, 0.5, 0.5, 1.0}, {0.25, 0.25, 0, 1.0},
    {0.2, 0.8, 0.1, 1.0}, {0.2, 0.9, 0.1, 1.0}, {0.8, 0.8, 0.0, 1.0},
    {0.9, 0.9, 0.9, 1.0}, {0.8, 0.5, 0.0, 1.0}, {0.0, 0.2, 0.8, 1.0},
    {0.1, 0.4, 0.1, 1.0}, {0.3, 0.1, 0.0, 1.0}, {1.0, 1.0, 1.0, 1.0},
    {1.0, 0.2, 0.0, 1.0}, {1.0, 0.0, 1.0, 1.0}
};

// object space color palette
vec3 os_palette(ivec3 m) {
    return vec3(
        m.x / float(CUNK_CHUNK_SIZE),
        m.y / float(CUNK_CHUNK_MAX_HEIGHT),
        m.z / float(CUNK_CHUNK_SIZE)
    );
}

bool inRange(ivec3 m) {
    return all(greaterThanEqual(vec3(m), vec3(0))) && all(lessThan(vec2(m.xz), vec2(CUNK_CHUNK_SIZE))) && m.y < CUNK_CHUNK_MAX_HEIGHT;
}

bool isBlock(ivec3 m) {
    return inRange(m) && push_constants.block_buffer.blocks[m.x][m.y][m.z] != 0;
}

// should only be used in dda after inRange check!
vec4 blockColor(uint ci) {
    // ci - color palette index
    if (ci < 14) {
        return color_palette[ci];
    }
    
    return vec4(0);
}

bool textureFrontFace(ivec3 m) {
    return inRange(m) && m.z == CUNK_CHUNK_SIZE - 1;
}

void main() {
    // colorOut = vec4(color, 1);
    // return;

    vec2 screen = gl_FragCoord.xy - vec2(0.5);

    ivec2 iscreen = ivec2(screen); // only used for debug buffer indexing

    vec2 ndc = screen / vec2(200) - vec2(1); // [-1, 1], 2 * (screen / image.xy) - 1
    ndc.y = -ndc.y;

    float depth = 1 / gl_FragCoord.w; // depth to the camera not the near plane

    vec3 cs = vec3(depth * ndc, -depth);


    vec4 ws = push_constants.trans_buffer.m_cs_ws * vec4(cs, 1); // camera translation

    vec4 os = ws - vec4(vec3(push_constants.chunk.xyz), 0);

    vec3 dir = normalize((push_constants.trans_buffer.m_cs_ws * vec4(cs, 0)).xyz);

    // object space
    vec3 pos = os.xyz;
    if (push_constants.inChunk) {
        pos = push_constants.trans_buffer.camera_pos - vec3(push_constants.chunk.xyz);
    }


    // clamp to [0, CUNK_CHUNK_SIZE) x [0, CUNK_CHUNK_MAX_HEIGHT) x [0, CUNK_CHUNK_SIZE)
    pos.x = clamp(pos.x, 0, CUNK_CHUNK_SIZE - EPSILON_CLAMP);
    pos.y = clamp(pos.y, 0, CUNK_CHUNK_MAX_HEIGHT - EPSILON_CLAMP);
    pos.z = clamp(pos.z, 0, CUNK_CHUNK_SIZE - EPSILON_CLAMP);

    ivec3 map = ivec3(pos);

    // show dir debug
    // colorOut = vec4(dir, 1.0);

    // show coverage
    // colorOut = vec4(float(inRange(map)));

    // show object space index
    // colorOut = vec4(os_palette(map), 1) * float(inRange(map));

    // show front face only
    // colorOut = vec4(float(textureFrontFace(map)));

    // dda
    vec3 deltaDist = abs(1 / dir);

    ivec3 rayStep = ivec3(sign(dir));

    vec3 sideDist =
        (sign(dir) * (vec3(map) - pos) + (sign(dir) * 0.5) + 0.5) * deltaDist;

    // debug saves
    push_constants.debug_buffer.vectors[iscreen.y*400 + iscreen.x] = vec4(rayStep, 99);
    push_constants.debug2_buffer.vectors[iscreen.y*400 + iscreen.x] = vec4(map, 99);


    bvec3 mask = bvec3(false, true, false);
    if (abs(pos.x - 0.0) < EPSILON_FACE || abs(pos.x - float(CUNK_CHUNK_SIZE)) < EPSILON_FACE) {
         mask = bvec3(true, false, false);
    } else if (abs(pos.y - 0.0) < EPSILON_FACE || abs(pos.y - float(CUNK_CHUNK_MAX_HEIGHT)) < EPSILON_FACE) {
        mask = bvec3(false, true, false);
    } else if (abs(pos.z - 0.0) < EPSILON_FACE || abs(pos.z - float(CUNK_CHUNK_SIZE)) < EPSILON_FACE) {
        mask = bvec3(false, false, true);
    }

    
    for (int i = 0; i < MAX_STEP; i++) {
        // if hit block
        if (isBlock(map)) {
            float shadow;
            float t;
            // fake shadow on sides
            if (mask.x) {
                shadow = 0.5;
                t = sideDist.x - deltaDist.x;
            }
            if (mask.y) {
                shadow = 1.0;
                t = sideDist.y - deltaDist.y;
            }
            if (mask.z) {
                shadow = 0.75;
                t = sideDist.z - deltaDist.z;
            }

            if (i == 0) {
                t = 0;
            }
            vec3 hit_os = pos + dir * t;

            vec3 hit_ws = hit_os + vec3(push_constants.chunk.xyz);

            vec4 ndc = push_constants.trans_buffer.mpp * vec4(hit_ws, 1.0);
            float ndc_z = ndc.z / ndc.w;
            gl_FragDepth = clamp(ndc_z, 0.0, 1.0);
            
            // colorOut = vec4(gl_FragDepth / 2);
            // mix shadow color with block color
            colorOut = shadow * blockColor(push_constants.block_buffer.blocks[map.x][map.y][map.z]);
            return;
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

    discard;
}


