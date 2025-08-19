#include "imr/imr.h"
#include "imr/util.h"
#include <iostream>

#include "chunk_mesh.h"
#include "world.h"

#include "nasl/nasl.h"
#include "nasl/nasl_mat.h"
#include <cmath>

#include "camera.h"
#include "threadpool.h"

using namespace nasl;

struct GPUChunk {
    int data[384][16][16];
};

struct PushConstants {
    VkDeviceAddress chunk_buffer;
    uint32_t chunk_count;
    int32_t min_cx;
    int32_t min_cz;
    uint32_t grid_w;
    vec3 pos;
    mat4 r;
} push_constants;

Camera camera = {.position =
                     {
                         0,
                         0,
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

int radius = 1;

int main(int argc, char **argv) {
    glfwInit();
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    auto window = glfwCreateWindow(1024, 1024, "Example", nullptr, nullptr);

    ThreadPool tp(std::thread::hardware_concurrency());

    if (argc < 2)
        return 0;

    glfwSetKeyCallback(window, [](GLFWwindow *window, int key, int scancode,
                                  int action, int mods) {
        if (key == GLFW_KEY_R && (mods & GLFW_MOD_CONTROL))
            reload_shaders = true;
        if (key == GLFW_KEY_PAGE_UP && action == GLFW_PRESS)
            radius++;
        if (key == GLFW_KEY_PAGE_DOWN && action == GLFW_PRESS)
            radius--;
    });

    imr::Context context;
    imr::Device device(context);
    imr::Swapchain swapchain(device, window);
    imr::FpsCounter fps_counter;

    auto world = World(argv[1]);

    auto prev_frame = imr_get_time_nano();
    float delta = 0;

    auto shaders = std::make_unique<Shaders>(device);

    auto load_chunk = [&](int cx, int cz) {
        auto loaded = world.get_loaded_chunk(cx, cz);
        if (!loaded)
            world.load_chunk(cx, cz);
    };

    auto packChunk = [&](int cx, int cz, GPUChunk &dst) {
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
                        dst.data[Y][x][z] = int(sec->block_data[x][y][z]);
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

                push_constants.pos = camera.position;
                push_constants.r = camera_to_world_rotation_matrix(&camera);

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

                const int gridW = 2 * radius + 1;
                const int gridH = 2 * radius + 1;

                const int player_chunk_x =
                    0; // int(std::floor(camera.position.x / 16.0f));
                const int player_chunk_z =
                    0; // int(std::floor(camera.position.z / 16.0f));

                const int min_cx = player_chunk_x - radius;
                const int min_cz = player_chunk_z - radius;

                const uint32_t chunk_count = gridW * gridH;

                for (int dx = -radius; dx <= radius; ++dx)
                    for (int dz = -radius; dz <= radius; ++dz)
                        load_chunk(player_chunk_x + dx, player_chunk_z + dz);

                std::vector<GPUChunk> gpu_chunks;
                gpu_chunks.resize(chunk_count);

                for (int gz = 0; gz < gridH; ++gz) {
                    for (int gx = 0; gx < gridW; ++gx) {
                        const int cx = min_cx + gx;
                        const int cz = min_cz + gz;
                        const size_t idx = size_t(gz) * gridW + gx;
                        packChunk(cx, cz, gpu_chunks[idx]);
                    }
                }

                const VkDeviceSize chunk_bytes =
                    VkDeviceSize(gpu_chunks.size()) * sizeof(GPUChunk);

                std::unique_ptr<imr::Buffer> chunk_buffer =
                    std::make_unique<imr::Buffer>(
                        device, chunk_bytes,
                        VK_BUFFER_USAGE_TRANSFER_DST_BIT |
                            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                            VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT);

                chunk_buffer->uploadDataSync(0, chunk_bytes, gpu_chunks.data());

                push_constants.chunk_buffer = chunk_buffer->device_address();
                push_constants.chunk_count = chunk_count;
                push_constants.min_cx = min_cx;
                push_constants.min_cz = min_cz;
                push_constants.grid_w = gridW;
                push_constants.pos = camera.position;
                push_constants.r = camera_to_world_rotation_matrix(&camera);

                vkCmdPushConstants(cmdbuf, dda_shader.layout(),
                                   VK_SHADER_STAGE_COMPUTE_BIT, 0,
                                   sizeof(push_constants), &push_constants);

                vkCmdDispatch(cmdbuf, (image.size().width + 31) / 32,
                              (image.size().height + 31) / 32, 1);

                context.addCleanupAction(
                    [=, &device]() { delete shader_bind_helper; });

                auto now = imr_get_time_nano();
                delta = ((float)((now - prev_frame) / 1000L)) / 1000000.0f;
                prev_frame = now;

                glfwPollEvents();
            });
    }

    swapchain.drain();
    return 0;
}
