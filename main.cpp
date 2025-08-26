#include <format>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "imr/imr.h"
#include "imr/util.h"

#include "world.h"

#include <cmath>

#include "nasl/nasl.h"
#include "nasl/nasl_mat.h"

#include "camera.h"
#define NUM_CHUNKS 1089
#define NUM_BLOCK_PER_CHUNK                                                    \
    CUNK_CHUNK_SIZE *CUNK_CHUNK_MAX_HEIGHT *CUNK_CHUNK_SIZE

using namespace nasl;

struct GPUChunk {
    int blocks[CUNK_CHUNK_SIZE][CUNK_CHUNK_MAX_HEIGHT][CUNK_CHUNK_SIZE];
};

struct {
    VkDeviceAddress chunk_buffer;
    vec3 pos;
    vec3 dir;
} push_constants_dda;

struct {
    VkDeviceAddress chunk_indices_buffer;
    VkDeviceAddress chunk_buffer;
    vec3 pos;
    mat4 r;
} push_constants_its;

Camera camera = {.position =
                     {
                         -1,
                         180,
                         -1,
                     },
                 .rotation = {0, M_PI_2},
                 .fov = 60};

CameraFreelookState camera_state = {
    .fly_speed = 100.0f,
    .mouse_sensitivity = 1,
};
CameraInput camera_input;

void camera_update(GLFWwindow *, CameraInput *input);

struct Shaders {
    imr::ComputePipeline its;
    imr::ComputePipeline dda;

    Shaders(imr::Device &d) : its(d, "its.spv"), dda(d, "dda.spv") {}
};

vec3 parseVec3(const std::string &input) {
    std::stringstream ss(input);
    std::string item;
    std::vector<float> values;

    while (std::getline(ss, item, ',')) {
        values.push_back(std::stof(item));
    }

    if (values.size() != 3) {
        throw std::invalid_argument("Expected 3 comma-separated floats");
    }

    return vec3(values[0], values[1], values[2]);
}

std::string print_vec3(const vec3 &v) {
    return std::format("({:.2f}, {:.2f}, {:.2f})", v[0], v[1], v[2]);
}

void updateDebugGlfwWindowTitle(GLFWwindow *window, int fps,
                                const Camera &camera) {
    glfwSetWindowTitle(window,
                       std::format("{}fps, {}, Y{:.2f}°, P{:.2f}°", fps,
                                   print_vec3(camera.position),
                                   camera.rotation.yaw * 180.0f / M_PI,
                                   camera.rotation.pitch * 180.0f / M_PI)
                           .c_str());
}

int main(int argc, char **argv) {
    glfwInit();
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    auto window = glfwCreateWindow(1024, 1024, "Example", nullptr, nullptr);

    if (argc < 2)
        return 0;

    imr::Context context;
    imr::Device device(context);
    imr::Swapchain swapchain(device, window);
    imr::FpsCounter fps_counter;

    auto world = World(argv[1]);
    if (argc > 2) {
        camera.position = parseVec3(argv[2]);
    }

    auto prev_frame = imr_get_time_nano();
    float delta = 0;

    int player_chunk_x = camera.position.x / 16;
    int player_chunk_z = camera.position.z / 16;

    // chunk positions (flat, no height)
    // ivec2 chunk_indices[NUM_CHUNKS] = {ivec2(0, 0), ivec2(0, 1)};

    ivec2 chunk_indices[NUM_CHUNKS] = {ivec2(1, 0)};
    for (int x = 0; x < 33; x++) {
        for (int z = 0; z < 33; z++) {
            chunk_indices[x * 33 + z] = ivec2(x, z);
        }
    }

    std::unique_ptr<imr::Buffer> chunk_indices_buffer =
        std::make_unique<imr::Buffer>(
            device, sizeof(chunk_indices),
            VK_BUFFER_USAGE_TRANSFER_DST_BIT |
                VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT);

    chunk_indices_buffer->uploadDataSync(0, chunk_indices_buffer->size,
                                         chunk_indices);
    push_constants_its.chunk_indices_buffer =
        chunk_indices_buffer->device_address();

    std::vector<GPUChunk> gpu_chunks;
    gpu_chunks.resize(NUM_CHUNKS);
    VkDeviceSize chunk_bytes = VkDeviceSize(NUM_CHUNKS * sizeof(GPUChunk));

    std::unique_ptr<imr::Buffer> chunk_buffer = std::make_unique<imr::Buffer>(
        device, chunk_bytes,
        VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
            VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT);

    auto pack_chunk = [&world, &device](int cx, int cz, GPUChunk &dst) {
        std::cout << std::format("Loading Chunk ({}, {})\n", cx, cz);
        world.load_chunk(cx, cz);
        Chunk *chunk = world.get_loaded_chunk(cx, cz);
        std::memset(&dst, 0, sizeof(GPUChunk));

        if (chunk == nullptr) {
            std::cout << "\tFailed to Load!\n";
        } else {

            int num_solid_block = 0;
            for (int s = 0; s < CUNK_CHUNK_SECTIONS_COUNT; s++) {
                if (chunk->data.sections[s] == nullptr)
                    continue;

                for (int x = 0; x < CUNK_CHUNK_SIZE; x++)
                    for (int ys = 0; ys < CUNK_CHUNK_SIZE; ys++)
                        for (int z = 0; z < CUNK_CHUNK_SIZE; z++) {
                            BlockData block =
                                chunk->data.sections[s]->block_data[ys][z][x];
                            if (block != BlockAir) {
                                int y = ys + s * CUNK_CHUNK_SIZE;
                                dst.blocks[x][y][z] = block;
                                num_solid_block++;
                            }
                        }
            }

            std::cout << std::format(
                "\tBlock stats: {:d} ({:.2f}\% full)\n", num_solid_block,
                num_solid_block / float(NUM_BLOCK_PER_CHUNK));
        }
    };

    for (int i = 0; i < NUM_CHUNKS; i++) {
        pack_chunk(chunk_indices[i][0], chunk_indices[i][1], gpu_chunks[i]);
    }

    chunk_buffer->uploadDataSync(0, chunk_bytes, gpu_chunks.data());
    push_constants_its.chunk_buffer = chunk_buffer->device_address();

    auto shaders = std::make_unique<Shaders>(device);

    auto &vk = device.dispatch;

    while (!glfwWindowShouldClose(window)) {
        fps_counter.tick();
        camera_update(window, &camera_input);
        camera_move_freelook(&camera, &camera_input, &camera_state, delta);
        push_constants_its.pos = camera.position;
        push_constants_its.r = camera_to_world_rotation_matrix(&camera);

        updateDebugGlfwWindowTitle(window, fps_counter.average_fps(), camera);

        swapchain.renderFrameSimplified(
            [&](imr::Swapchain::SimplifiedRenderContext &context) {
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

                auto &its_shader = shaders->its;
                vkCmdBindPipeline(cmdbuf, VK_PIPELINE_BIND_POINT_COMPUTE,
                                  its_shader.pipeline());

                auto shader_bind_helper = its_shader.create_bind_helper();
                shader_bind_helper->set_storage_image(0, 0, image);
                shader_bind_helper->commit(cmdbuf);

                vkCmdPushConstants(
                    cmdbuf, its_shader.layout(), VK_SHADER_STAGE_COMPUTE_BIT, 0,
                    sizeof(push_constants_its), &push_constants_its);

                // render only one chunk

                vkCmdDispatch(cmdbuf, (image.size().width + 31) / 32,
                              (image.size().height + 31) / 32, 1);

                context.addCleanupAction(
                    [=, &device]() { delete shader_bind_helper; });

                auto now = imr_get_time_nano();
                delta = ((float)((now - prev_frame) / 1000L)) / 1000000.0f;
                prev_frame = now;
            });

        glfwPollEvents();
    }

    swapchain.drain();
    return 0;
}
