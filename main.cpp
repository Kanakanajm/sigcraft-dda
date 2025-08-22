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

struct GPUChunk {
    int data[384][16][16];
};

struct PushConstants {
    VkDeviceAddress chunk_buffer;
    int chunk_count;
    int radius;
    ivec2 chunk_pos;
    vec3 pos;
    mat4 r;
} push_constants;

Camera camera = {.position =
                     {
                         0,
                         150,
                         0,
                     },
                 .rotation = {0, 0},
                 .fov = 60};

CameraFreelookState camera_state = {
    .fly_speed = 100.0f,
    .mouse_sensitivity = 1,
};

CameraInput camera_input;

void camera_update(GLFWwindow *, CameraInput *input);

bool reload_shaders = false;

struct Shaders {
    imr::ComputePipeline dda;

    Shaders(imr::Device &d) : dda(d, "dda.spv") {}
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

    const int radius = 4;
    const int grid_size = 2 * radius + 1;
    const int chunk_count = grid_size * grid_size;

    std::vector<GPUChunk> gpu_chunks;
    gpu_chunks.resize(chunk_count);

    VkDeviceSize chunk_bytes = VkDeviceSize(chunk_count * 384 * 16 * 16);

    std::unique_ptr<imr::Buffer> chunk_buffer = std::make_unique<imr::Buffer>(
        device, chunk_bytes,
        VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
            VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT);

    auto prev_frame = imr_get_time_nano();
    float delta = 0;

    auto shaders = std::make_unique<Shaders>(device);

    auto load_chunk = [&](int cx, int cz) {
        auto loaded = world.get_loaded_chunk(cx, cz);
        if (!loaded)
            world.load_chunk(cx, cz);
    };

    auto pack_chunk = [&](int cx, int cz, GPUChunk &dst) {
        std::memset(&dst, 0, sizeof(GPUChunk));

        auto ch = world.get_loaded_chunk(cx, cz);
        if (!ch)
            return;

        for (unsigned int s = 0; s < CUNK_CHUNK_SECTIONS_COUNT; ++s) {
            ChunkSection *sec = ch->data.sections[s];
            if (!sec)
                continue;

            for (unsigned int x = 0; x < CUNK_CHUNK_SIZE; ++x)
                for (unsigned int y = 0; y < CUNK_CHUNK_SIZE; ++y)
                    for (unsigned int z = 0; z < CUNK_CHUNK_SIZE; ++z) {
                        const unsigned int Y = y + s * CUNK_CHUNK_SIZE;
                        dst.data[Y][x][z] = sec->block_data[y][z][x];
                    }
        }
    };

    auto &vk = device.dispatch;
    while (!glfwWindowShouldClose(window)) {
        fps_counter.tick();
        fps_counter.updateGlfwWindowTitle(window);

        swapchain.renderFrameSimplified(
            [&](imr::Swapchain::SimplifiedRenderContext &context) {
                camera_update(window, &camera_input);
                camera_move_freelook(&camera, &camera_input, &camera_state,
                                     delta);

                if (reload_shaders) {
                    swapchain.drain();
                    shaders = std::make_unique<Shaders>(device);
                    reload_shaders = false;
                }

                auto &image = context.image();
                auto cmdbuf = context.cmdbuf();

                vk.cmdClearColorImage(
                    cmdbuf, image.handle(), VK_IMAGE_LAYOUT_GENERAL,
                    tmpPtr((VkClearColorValue){
                        .float32 = {0.0f, 0.0f, 0.0f, 1.0f},
                    }),
                    1, tmpPtr(image.whole_image_subresource_range()));

                vk.cmdPipelineBarrier2(
                    cmdbuf,
                    tmpPtr((VkDependencyInfo){
                        .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
                        .dependencyFlags = 0,
                        .memoryBarrierCount = 1,
                        .pMemoryBarriers = tmpPtr((VkMemoryBarrier2){
                            .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2,
                            .srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                            .srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT,
                            .dstStageMask =
                                VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                            .dstAccessMask =
                                VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
                        })}));

                auto &dda_shader = shaders->dda;
                vkCmdBindPipeline(cmdbuf, VK_PIPELINE_BIND_POINT_COMPUTE,
                                  dda_shader.pipeline());

                auto shader_bind_helper = dda_shader.create_bind_helper();
                shader_bind_helper->set_storage_image(0, 0, image);
                shader_bind_helper->commit(cmdbuf);

                int player_chunk_x = int(std::floor(camera.position.x / 16.0f));
                int player_chunk_z = int(std::floor(camera.position.z / 16.0f));

                ivec2 chunk_pos = ivec2(player_chunk_x, player_chunk_z);

                for (int dx = -radius; dx <= radius; ++dx)
                    for (int dz = -radius; dz <= radius; ++dz)
                        load_chunk(player_chunk_x + dx, player_chunk_z + dz);

                for (auto chunk : world.loaded_chunks()) {
                    if (abs(chunk->cx - player_chunk_x) > radius ||
                        abs(chunk->cz - player_chunk_z) > radius) {
                        world.unload_chunk(chunk.get());
                    }
                }

                int min_cx = player_chunk_x - radius;
                int min_cz = player_chunk_z - radius;

                for (int gz = 0; gz < grid_size; ++gz) {
                    for (int gx = 0; gx < grid_size; ++gx) {
                        int cx = min_cx + gx;
                        int cz = min_cz + gz;
                        pack_chunk(cx, cz, gpu_chunks[gz * grid_size + gx]);
                    }
                }
                
                chunk_buffer->uploadDataSync(0, chunk_bytes, gpu_chunks.data());

                push_constants.chunk_buffer = chunk_buffer->device_address();
                push_constants.chunk_count = chunk_count;
                push_constants.radius = radius;
                push_constants.chunk_pos = chunk_pos;
                push_constants.pos = camera.position;
                push_constants.r = camera_to_world_rotation_matrix(&camera);

                vkCmdPushConstants(cmdbuf, dda_shader.layout(),
                                   VK_SHADER_STAGE_COMPUTE_BIT, 0,
                                   sizeof(push_constants), &push_constants);

                vkCmdDispatch(cmdbuf, (image.size().width + 31) / 32,
                              (image.size().height + 31) / 32, 1);

                auto now = imr_get_time_nano();
                delta = ((float)((now - prev_frame) / 1000L)) / 1000000.0f;
                prev_frame = now;

                glfwPollEvents();
            });
    }

    swapchain.drain();
    return 0;
}
