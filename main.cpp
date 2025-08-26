#include <cmath>
#include <iostream>

#include "imr/imr.h"
#include "imr/util.h"
#include "nasl/nasl.h"
#include "nasl/nasl_mat.h"

#include "camera.h"
#include "chunk_mesh.h"
#include "world.h"

using namespace nasl;

struct Tri {
    vec3 v0, v1, v2;
    vec3 color;
};

struct Cube {
    Tri triangles[12];
};

Cube make_cube() {
    /*
     *  +Y
     *  ^
     *  |
     *  |
     *  D------C.
     *  |\     |\
     *  | H----+-G
     *  | |    | |
     *  A-+----B | ---> +X
     *   \|     \|
     *    E------F
     *     \
     *      \
     *       \
     *        v +Z
     *
     * Adapted from
     * https://www.asciiart.eu/art-and-design/geometries
     */
    vec3 A = {0, 0, 0};
    vec3 B = {CUNK_CHUNK_SIZE, 0, 0};
    vec3 C = {CUNK_CHUNK_SIZE, CUNK_CHUNK_MAX_HEIGHT, 0};
    vec3 D = {0, CUNK_CHUNK_MAX_HEIGHT, 0};
    vec3 E = {0, 0, CUNK_CHUNK_SIZE};
    vec3 F = {CUNK_CHUNK_SIZE, 0, CUNK_CHUNK_SIZE};
    vec3 G = {CUNK_CHUNK_SIZE, CUNK_CHUNK_MAX_HEIGHT, CUNK_CHUNK_SIZE};
    vec3 H = {0, CUNK_CHUNK_MAX_HEIGHT, CUNK_CHUNK_SIZE};

    int i = 0;
    Cube cube = {};

    auto add_face = [&](vec3 v0, vec3 v1, vec3 v2, vec3 v3, vec3 color) {
        /*
         * v0 --- v3
         *  |   / |
         *  |  /  |
         *  | /   |
         * v1 --- v2
         */
        cube.triangles[i++] = {v0, v1, v3, color};
        cube.triangles[i++] = {v1, v2, v3, color};
    };

    // top face
    add_face(H, D, C, G, vec3(0, 1, 0));
    // north face
    add_face(A, B, C, D, vec3(1, 0, 0));
    // west face
    add_face(A, D, H, E, vec3(0, 0, 1));
    // east face
    add_face(F, G, C, B, vec3(1, 0, 1));
    // south face
    add_face(E, H, G, F, vec3(0, 1, 1));
    // bottom face
    add_face(E, F, B, A, vec3(1, 1, 0));
    assert(i == 12);
    return cube;
}

struct {
    VkDeviceAddress vertex_buffer;
    VkDeviceAddress chunk_buffer;
    vec3 chunk_position;
    mat4 matrix;
    mat4 inv_matrix;
    vec3 camera_pos;

} push_constants;

struct GPUChunk {
    ivec2 location;
    VkDeviceAddress chunk_buffer;
};

Camera camera = {.position =
                     {
                         24,
                         192,
                         24,
                     },
                 .rotation = {0, M_PI_2},
                 .fov = 60};

CameraFreelookState camera_state = {
    .fly_speed = 100.0f,
    .mouse_sensitivity = 1,
};

CameraInput camera_input;

void camera_update(GLFWwindow *, CameraInput *input);

bool reload_shaders = false;

struct Shaders {
    std::vector<std::string> files = {"basic.vert.spv", "basic.frag.spv"};

    std::vector<std::unique_ptr<imr::ShaderModule>> modules;
    std::vector<std::unique_ptr<imr::ShaderEntryPoint>> entry_points;
    std::unique_ptr<imr::GraphicsPipeline> pipeline;

    Shaders(imr::Device &d, imr::Swapchain &swapchain) {
        imr::GraphicsPipeline::RenderTargetsState rts;
        rts.color.push_back((imr::GraphicsPipeline::RenderTarget){
            .format = swapchain.format(),
            .blending = {.blendEnable = false,
                         .colorWriteMask = VK_COLOR_COMPONENT_R_BIT |
                                           VK_COLOR_COMPONENT_G_BIT |
                                           VK_COLOR_COMPONENT_B_BIT |
                                           VK_COLOR_COMPONENT_A_BIT}});
        imr::GraphicsPipeline::RenderTarget depth = {.format =
                                                         VK_FORMAT_D32_SFLOAT};
        rts.depth = depth;

        imr::GraphicsPipeline::StateBuilder stateBuilder = {
            .vertexInputState = imr::GraphicsPipeline::no_vertex_input(),
            .inputAssemblyState =
                imr::GraphicsPipeline::simple_triangle_input_assembly(),
            .viewportState =
                imr::GraphicsPipeline::one_dynamically_sized_viewport(),
            .rasterizationState =
                imr::GraphicsPipeline::solid_filled_polygons(),
            .multisampleState = imr::GraphicsPipeline::one_spp(),
            .depthStencilState = imr::GraphicsPipeline::simple_depth_testing(),
        };

        std::vector<imr::ShaderEntryPoint *> entry_point_ptrs;
        for (auto filename : files) {
            VkShaderStageFlagBits stage;
            if (filename.ends_with("vert.spv"))
                stage = VK_SHADER_STAGE_VERTEX_BIT;
            else if (filename.ends_with("frag.spv"))
                stage = VK_SHADER_STAGE_FRAGMENT_BIT;
            else
                throw std::runtime_error("Unknown suffix");
            modules.push_back(
                std::make_unique<imr::ShaderModule>(d, std::move(filename)));
            entry_points.push_back(std::make_unique<imr::ShaderEntryPoint>(
                *modules.back(), stage, "main"));
            entry_point_ptrs.push_back(entry_points.back().get());
        }
        pipeline = std::make_unique<imr::GraphicsPipeline>(
            d, std::move(entry_point_ptrs), rts, stateBuilder);
    }
};

int main(int argc, char **argv) {
    glfwInit();
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    auto window = glfwCreateWindow(1024, 1024, "Example", nullptr, nullptr);

    if (argc < 2)
        return 0;

    glfwSetKeyCallback(window, [](GLFWwindow *window, int key, int scancode,
                                  int action, int mods) {
        if (key == GLFW_KEY_R && (mods & GLFW_MOD_CONTROL))
            reload_shaders = true;
        // if (key == GLFW_KEY_PAGE_UP && action == GLFW_PRESS)
        //     radius++;
        // if (key == GLFW_KEY_PAGE_DOWN && action == GLFW_PRESS)
        //     radius--;
    });

    imr::Context context;
    imr::Device device(context);
    imr::Swapchain swapchain(device, window);
    imr::FpsCounter fps_counter;

    auto world = World(argv[1]);

    const int radius = 0;
    const int grid_size = 2 * radius + 1;
    const int chunk_count = grid_size * grid_size;

    std::unique_ptr<imr::Buffer> vertex_buffer = std::make_unique<imr::Buffer>(
        device, sizeof(vec3) * 3 * 12 * 2,
        VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
            VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT);

    // Chunk cube are the same for every chunk, the only thing differs is the
    // transformation that will be passed into vertex shader as (transformation)
    // matrix
    Cube cube = make_cube();
    std::vector<vec3> vertex_vector;
    for (auto &tri : cube.triangles) {
        vertex_vector.push_back(tri.v0);
        vertex_vector.push_back(tri.v1);
        vertex_vector.push_back(tri.v2);
    }
    for (auto &tri : cube.triangles) {
        vertex_vector.push_back(tri.color);
        vertex_vector.push_back(tri.color);
        vertex_vector.push_back(tri.color);
    }
    push_constants.vertex_buffer = vertex_buffer->device_address();
    vertex_buffer->uploadDataSync(0, vertex_buffer->size, vertex_vector.data());

    auto prev_frame = imr_get_time_nano();
    float delta = 0;

    auto shaders = std::make_unique<Shaders>(device, swapchain);

    std::unique_ptr<imr::Image> depthBuffer;

    auto &vk = device.dispatch;
    while (!glfwWindowShouldClose(window)) {
        fps_counter.tick();
        fps_counter.updateGlfwWindowTitle(window);

        swapchain.renderFrameSimplified([&](imr::Swapchain::
                                                SimplifiedRenderContext
                                                    &context) {
            camera_update(window, &camera_input);
            camera_move_freelook(&camera, &camera_input, &camera_state, delta);
            push_constants.camera_pos = camera.position;

            if (reload_shaders) {
                swapchain.drain();
                shaders = std::make_unique<Shaders>(device, swapchain);
                reload_shaders = false;
            }

            auto &image = context.image();
            auto cmdbuf = context.cmdbuf();

            if (!depthBuffer ||
                depthBuffer->size().width != context.image().size().width ||
                depthBuffer->size().height != context.image().size().height) {
                VkImageUsageFlagBits depthBufferFlags =
                    static_cast<VkImageUsageFlagBits>(
                        VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                        VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT);
                depthBuffer = std::make_unique<imr::Image>(
                    device, VK_IMAGE_TYPE_2D, context.image().size(),
                    VK_FORMAT_D32_SFLOAT, depthBufferFlags);

                vk.cmdPipelineBarrier2KHR(
                    cmdbuf,
                    tmpPtr((VkDependencyInfo){
                        .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
                        .dependencyFlags = 0,
                        .imageMemoryBarrierCount = 1,
                        .pImageMemoryBarriers = tmpPtr((VkImageMemoryBarrier2){
                            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
                            .srcStageMask = 0,
                            .srcAccessMask = 0,
                            .dstStageMask =
                                VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                            .dstAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT |
                                             VK_ACCESS_2_MEMORY_READ_BIT,
                            .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
                            .newLayout = VK_IMAGE_LAYOUT_GENERAL,
                            .image = depthBuffer->handle(),
                            .subresourceRange =
                                depthBuffer
                                    ->whole_image_subresource_range()})}));
            }

            vk.cmdClearColorImage(
                cmdbuf, image.handle(), VK_IMAGE_LAYOUT_GENERAL,
                tmpPtr((VkClearColorValue){
                    .float32 = {0.0f, 0.0f, 0.0f, 1.0f},
                }),
                1, tmpPtr(image.whole_image_subresource_range()));

            vk.cmdClearDepthStencilImage(
                cmdbuf, depthBuffer->handle(), VK_IMAGE_LAYOUT_GENERAL,
                tmpPtr((VkClearDepthStencilValue){
                    .depth = 1.0f,
                    .stencil = 0,
                }),
                1, tmpPtr(depthBuffer->whole_image_subresource_range()));

            vk.cmdPipelineBarrier2KHR(
                cmdbuf,
                tmpPtr((VkDependencyInfo){
                    .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
                    .dependencyFlags = 0,
                    .memoryBarrierCount = 1,
                    .pMemoryBarriers = tmpPtr((VkMemoryBarrier2){
                        .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2,
                        .srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                        .srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT,
                        .dstStageMask = VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT,
                        .dstAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT |
                                         VK_ACCESS_2_MEMORY_READ_BIT,
                    })}));

            // update the push constant data on the host...
            mat4 m = identity_mat4;
            mat4 flip_y = identity_mat4;
            flip_y.rows[1][1] = -1;
            m = m * flip_y;
            mat4 view_mat =
                camera_get_view_mat4(&camera, context.image().size().width,
                                     context.image().size().height);
            m = m * view_mat;
            m = m * translate_mat4(vec3(-0.5, -0.5f, -0.5f));

            auto &pipeline = shaders->pipeline;
            vkCmdBindPipeline(cmdbuf, VK_PIPELINE_BIND_POINT_GRAPHICS,
                              pipeline->pipeline());

            context.frame().withRenderTargets(
                cmdbuf, {&image}, &*depthBuffer, [&]() {
                    int player_chunk_x =
                        int(std::floor(camera.position.x / 16.0f));
                    int player_chunk_z =
                        int(std::floor(camera.position.z / 16.0f));

                    ivec2 chunk_pos = ivec2(player_chunk_x, player_chunk_z);

                    for (auto chunk : world.loaded_chunks()) {
                        if (abs(chunk->cx - player_chunk_x) > radius ||
                            abs(chunk->cz - player_chunk_z) > radius) {
                            world.unload_chunk(chunk.get());
                        }
                    }

                    auto load_chunk = [&](int cx, int cz) {
                        auto loaded = world.get_loaded_chunk(cx, cz);
                        if (!loaded)
                            world.load_chunk(cx, cz);
                    };

                    /*
                    00 01 02
                    10 11 12
                    20 21 22
                    */
                    // 11 - player pos / chunk pos
                    int min_cx = player_chunk_x - radius;
                    int min_cz = player_chunk_z - radius;

                    std::vector<GPUChunk> chunks;
                    for (int dx = 0; dx < grid_size; ++dx) {
                        for (int dz = 0; dz < grid_size; ++dz) {
                            int cx = min_cx + dx;
                            int cz = min_cz + dz;
                            load_chunk(cx, cz);
                            auto ch = world.get_loaded_chunk(cx, cz);
                            if (ch) {
                                GPUChunk chunk = {.location = ivec2(cx, cz)};
                                int block_data[CUNK_CHUNK_MAX_HEIGHT]
                                              [CUNK_CHUNK_SIZE]
                                              [CUNK_CHUNK_SIZE] = {};
                                for (size_t s = 0;
                                     s < CUNK_CHUNK_SECTIONS_COUNT; ++s) {
                                    ChunkSection *sec = ch->data.sections[s];
                                    if (!sec)
                                        continue;

                                    for (size_t x = 0; x < CUNK_CHUNK_SIZE; ++x)
                                        for (size_t y = 0; y < CUNK_CHUNK_SIZE;
                                             ++y)
                                            for (size_t z = 0;
                                                 z < CUNK_CHUNK_SIZE; ++z) {
                                                const unsigned int Y =
                                                    y + s * CUNK_CHUNK_SIZE;
                                                block_data[Y][x][z] =
                                                    sec->block_data[y][z][x];
                                            }
                                }

                                std::unique_ptr<imr::Buffer> chunk_buffer =
                                    std::make_unique<imr::Buffer>(
                                        device, sizeof(block_data),
                                        VK_BUFFER_USAGE_TRANSFER_DST_BIT |
                                            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                                            VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT);

                                chunk_buffer->uploadDataSync(
                                    0, chunk_buffer->size, block_data);
                                chunk.chunk_buffer =
                                    chunk_buffer->device_address();
                                chunks.push_back(chunk);
                            }
                        }
                    }

                    push_constants.matrix = m;
                    push_constants.inv_matrix = invert_mat4(m);
                    for (GPUChunk chunk : chunks) {
                        push_constants.chunk_position = {
                            chunk.location[0] * float(CUNK_CHUNK_SIZE), 0,
                            chunk.location[1] * float(CUNK_CHUNK_SIZE)};
                        push_constants.chunk_buffer = chunk.chunk_buffer;

                        vkCmdPushConstants(cmdbuf, pipeline->layout(),
                                           VK_SHADER_STAGE_VERTEX_BIT |
                                               VK_SHADER_STAGE_FRAGMENT_BIT,
                                           0, sizeof(push_constants),
                                           &push_constants);
                        vkCmdDraw(cmdbuf, 12 * 3, 1, 0, 0);
                    }
                });

            auto now = imr_get_time_nano();
            delta = ((float)((now - prev_frame) / 1000L)) / 1000000.0f;
            prev_frame = now;

            glfwPollEvents();
        });
    }

    swapchain.drain();
    return 0;
}
