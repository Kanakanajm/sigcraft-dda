#version 450
#extension GL_EXT_shader_image_load_formatted : require
#extension GL_EXT_scalar_block_layout : require
#extension GL_EXT_buffer_reference : require

#define CUNK_CHUNK_SIZE 16
#define CUNK_CHUNK_MAX_HEIGHT 384

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
    vec3 chunk_position;
    mat4 matrix;
    mat4 inv_matrix;
    vec3 camera_pos;
    ivec2 window_size;
}
push_constants;

void main() {
    mat4 matrix = push_constants.matrix;
    vec3 vertex = push_constants.vertex_buffer.vertices[gl_VertexIndex];
    gl_Position =
        matrix * vec4(vec3(vertex + push_constants.chunk_position), 1.0);
}