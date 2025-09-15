#version 450
#extension GL_EXT_shader_image_load_formatted : require
#extension GL_EXT_scalar_block_layout : require
#extension GL_EXT_buffer_reference : require
#define CUNK_CHUNK_SIZE 16
#define CUNK_CHUNK_MAX_HEIGHT 384

layout(location = 0)
out vec3 color;

layout(scalar, buffer_reference) buffer VertexBuffer {
    vec3 vertices[36];
    vec3 vertexColors[36];
};

layout(scalar, buffer_reference) buffer DebugBuffer {
    vec4 vectors[400*400];
};

layout(scalar, buffer_reference) buffer TransformBuffer {
    mat4 mpp; // perspective projection matrix, world space -> clip space
    mat4 mpp_inv; // inverse of perspective projection matrix, clip space -> world space
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
    float rotate_rad;
} push_constants;

void main() {
    mat4 rotateY = mat4(mat3(
        cos(push_constants.rotate_rad), 0.0, sin(push_constants.rotate_rad),
        0.0, 1.0, 0.0,
        -sin(push_constants.rotate_rad), 0.0, cos(push_constants.rotate_rad)
    ));
    mat4 matrix = push_constants.trans_buffer.mpp;
    int vtx_id = gl_VertexIndex;
    if (push_constants.inChunk) {
        if (vtx_id % 3 == 1)
            vtx_id += 1;
        else if (vtx_id % 3 == 2) 
            vtx_id -= 1;
    }
    vec3 vertex = push_constants.vertex_buffer.vertices[vtx_id];
    gl_Position = matrix * (rotateY * vec4(vertex, 1.0) + vec4(push_constants.chunk.xyz, 0));
    color = push_constants.vertex_buffer.vertexColors[vtx_id];
}