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
#define RADIUS 1
#define NUM_CHUNKS_PER_AXIS (2 * RADIUS + 1)
#define NUM_CHUNKS NUM_CHUNKS_PER_AXIS * NUM_CHUNKS_PER_AXIS

using namespace nasl;

struct {
    VkDeviceAddress chunk_buffer;
    ivec2 chunk_pos;
    mat4 m;
    float fov;
    vec4 color_palette[14] = {
        {0.0, 0.0, 0.0, 1.0}, {0.5, 0.5, 0.5, 1.0}, {0.25, 0.25, 0, 1.0},
        {0.2, 0.8, 0.1, 1.0}, {0.2, 0.9, 0.1, 1.0}, {0.8, 0.8, 0.0, 1.0},
        {0.9, 0.9, 0.9, 1.0}, {0.8, 0.5, 0.0, 1.0}, {0.0, 0.2, 0.8, 1.0},
        {0.1, 0.4, 0.1, 1.0}, {0.3, 0.1, 0.0, 1.0}, {1.0, 1.0, 1.0, 1.0},
        {1.0, 0.2, 0.0, 1.0}, {1.0, 0.0, 1.0, 1.0}};
} push_constants_old;

struct {
    VkDeviceAddress chunk_buffer;
    ivec2 chunk_pos;
    vec3 pos;
    mat4 r;
} push_constants;

Camera camera = {.position =
                     {
                         32,
                         128,
                         32,
                     },
                 .rotation = {0, 1.5708},
                 .fov = 60};
CameraFreelookState camera_state = {
    .fly_speed = 100.0f,
    .mouse_sensitivity = 1,
};
CameraInput camera_input;

void camera_update(GLFWwindow *, CameraInput *input);

struct Shaders {
    imr::ComputePipeline dda;

    Shaders(imr::Device &d) : dda(d, "dda.spv") {}
};

int radius = 16;

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

    push_constants.chunk_pos = ivec2(player_chunk_x, player_chunk_z);

    // chunk position (flat, no height)

    // populate chunk data
    int chunk_data[NUM_CHUNKS][384][16][16];
    std::memset(&chunk_data, 0, sizeof(chunk_data));
    for (int dx = -RADIUS; dx < RADIUS; dx++)
        for (int dz = -RADIUS; dz < RADIUS; dz++) {
            int num_solid_chuck = 0;

            int cx = player_chunk_x + dx;
            int cz = player_chunk_z + dz;
            world.load_chunk(cx, cz);
            auto chunk = world.get_loaded_chunk(cx, cz);
            if (!chunk) {
                std::cout << "Chunk at (" << cx << ", " << cz
                          << ") is not loaded\n";
                return 0;
            }

            std::cout << "World loaded chunk at (" << cx << ", " << cz << ")\n";
            for (int section = 0; section < CUNK_CHUNK_SECTIONS_COUNT;
                 section++) {
                if (chunk->data.sections[section] == 0)
                    continue;
                for (int x = 0; x < CUNK_CHUNK_SIZE; x++)
                    for (int y = 0; y < CUNK_CHUNK_SIZE; y++)
                        for (int z = 0; z < CUNK_CHUNK_SIZE; z++) {
                            int world_y = y + section * CUNK_CHUNK_SIZE;
                            BlockData block_data =
                                chunk->data.sections[section]
                                    ->block_data[y][z][x];
                            if (block_data != BlockAir) {
                                chunk_data[(dx + RADIUS) * NUM_CHUNKS_PER_AXIS + (dz + RADIUS)][world_y][x][z] = block_data;
                                num_solid_chuck++;
                            }
                        }
            }

            std::cout << "Chunk data copied: " << num_solid_chuck
                      << " / 98304\n";
        }

    std::unique_ptr<imr::Buffer> chunk_buffer = std::make_unique<imr::Buffer>(
        device, sizeof(chunk_data),
        VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
            VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT);

    // load chunk into buffer
    chunk_buffer->uploadDataSync(0, chunk_buffer->size, chunk_data);
    push_constants.chunk_buffer = chunk_buffer->device_address();

    std::cout << "Chunk loaded." << std::endl;

    auto shaders = std::make_unique<Shaders>(device);

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

                vkCmdPushConstants(cmdbuf, dda_shader.layout(),
                                   VK_SHADER_STAGE_COMPUTE_BIT, 0,
                                   sizeof(push_constants), &push_constants);

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
