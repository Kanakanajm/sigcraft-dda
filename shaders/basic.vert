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
    uint blocks[CUNK_CHUNK_MAX_HEIGHT*CUNK_CHUNK_SIZE*CUNK_CHUNK_SIZE];
};

layout(scalar, push_constant) uniform T {
	VertexBuffer vertex_buffer;
    DebugBuffer debug_buffer;
    DebugBuffer debug2_buffer;
    TransformBuffer trans_buffer;
    BlockBuffer block_buffer;
    ivec4 chunk; // (cx, cz, id, inChunk)
} push_constants;

void main() {
    vec3 chunk_pos = vec3(push_constants.chunk.x * CUNK_CHUNK_SIZE, 0, push_constants.chunk.y * CUNK_CHUNK_SIZE);

    // object space
    vec3 vertex_pos = push_constants.vertex_buffer.vertices[gl_VertexIndex];

    gl_Position =  push_constants.trans_buffer.mpp * vec4(vertex_pos + chunk_pos, 1.0);

    color = push_constants.vertex_buffer.vertexColors[gl_VertexIndex];
}