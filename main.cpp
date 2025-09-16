#include <cmath>
#include <format>
#include <iostream>
#include <unordered_map>

#include "imr/imr.h"
#include "imr/util.h"
#include "nasl/nasl.h"
#include "nasl/nasl_mat.h"

#include "camera.h"
#include "chunk_mesh.h"
#include "world.h"

#define CUNK_HSLICE_SIZE (CUNK_CHUNK_SIZE*CUNK_CHUNK_SIZE)
#define CUNK_SIZE (CUNK_HSLICE_SIZE*CUNK_CHUNK_MAX_HEIGHT)

#define RADIUS 1
#define GRID_SIZE (2*RADIUS + 1)
#define NUM_CHUNKS (GRID_SIZE*GRID_SIZE)

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
    add_face(A, B, C, D, vec3(0, 0, 1));
    // west face
    add_face(A, D, H, E, vec3(1, 0, 0));
    // east face
    add_face(F, G, C, B, vec3(1, 0, 0));
    // south face
    add_face(E, H, G, F, vec3(0, 0, 1));
    // bottom face
    add_face(E, F, B, A, vec3(0, 1, 0));
    assert(i == 12);
    return cube;
}

struct {
    mat4 mpp = identity_mat4; // perspective projection matrix, world space -> clip space
    mat4 mpp_inv = identity_mat4; // inverse of perspective projection matrix, clip space -> world space
    vec3 camera_pos;
} transform_matrices;

struct {
    VkDeviceAddress vertex_buffer;
    VkDeviceAddress debug_buffer;
    VkDeviceAddress debug2_buffer;
    VkDeviceAddress trans_buffer;
    VkDeviceAddress block_buffer;
    ivec4 chunk;
    uint chunk_index;
} push_constants;

struct GPUChunk {
    ivec2 location;
    bool loaded = false;
};

struct Ivec2Key {
    int x;
    int y;
};

bool operator==(const Ivec2Key &lhs, const Ivec2Key &rhs) {
    return lhs.x == rhs.x && lhs.y == rhs.y;
}

template<>
struct std::hash<Ivec2Key>
{
    std::size_t operator()(const Ivec2Key& k) const noexcept
    {
        std::size_t h1 = std::hash<int>{}(k.x);
        std::size_t h2 = std::hash<int>{}(k.y);
        return h1 ^ (h2 << 1); // or use boost::hash_combine
    }
};

Camera camera = {.position =
                     {
                         0,
                         385,
                         0,
                     },
                 .rotation = {0, M_PI_2},
                 .fov = 90};

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
                VkPipelineRasterizationStateCreateInfo{
                    .sType =
                        VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,

                    .polygonMode = VK_POLYGON_MODE_FILL,
                    .cullMode = VK_CULL_MODE_NONE,
                    .frontFace = VK_FRONT_FACE_CLOCKWISE,

                    .lineWidth = 1.0f,
                },
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

std::string print_vec3(const vec3 &v) {
    return std::format("({:.2f}, {:.2f}, {:.2f})", v[0], v[1], v[2]);
}

void updateDebugGlfwWindowTitle(GLFWwindow *window, const ivec2& chunk_pos, int fps) {
    glfwSetWindowTitle(window,
                       std::format("{}, ({}, {})", fps, chunk_pos[0], chunk_pos[1])
                           .c_str());
}

int main(int argc, char **argv) {
    glfwInit();
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);

    auto window = glfwCreateWindow(400, 400, "Example", nullptr, nullptr);

    if (argc < 2)
        return 0;

    glfwSetKeyCallback(window, [](GLFWwindow *window, int key, int scancode,
                                  int action, int mods) {
        if (key == GLFW_KEY_R && (mods & GLFW_MOD_CONTROL))
            reload_shaders = true;
    });

    imr::Context context;
    imr::Device device(context, [](vkb::PhysicalDeviceSelector& selector) {
        // Customize the selector here
        selector.set_name("NVIDIA GeForce MX250");
    });
    imr::Swapchain swapchain(device, window);
    imr::FpsCounter fps_counter;

    auto world = World(argv[1]);


    ivec2 center_chunk_pos = ivec2(0, 0);
    
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


    vec4 debug_vectors[400*400] = { vec4(0) };

    std::unique_ptr<imr::Buffer> debug_buffer = std::make_unique<imr::Buffer>(device, sizeof(debug_vectors), VK_BUFFER_USAGE_TRANSFER_DST_BIT  | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT);
    push_constants.debug_buffer = debug_buffer->device_address();
    
    vec4 debug2_vectors[400*400] = { vec4(0) };

    std::unique_ptr<imr::Buffer> debug2_buffer = std::make_unique<imr::Buffer>(device, sizeof(debug2_vectors), VK_BUFFER_USAGE_TRANSFER_DST_BIT  | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT);
    push_constants.debug2_buffer = debug2_buffer->device_address();
    

    std::unique_ptr<imr::Buffer> trans_buffer = std::make_unique<imr::Buffer>(device, sizeof(transform_matrices), VK_BUFFER_USAGE_TRANSFER_DST_BIT  | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT);
    push_constants.trans_buffer = trans_buffer->device_address();

    auto prev_frame = imr_get_time_nano();
    float delta = 0;

    auto shaders = std::make_unique<Shaders>(device, swapchain);

    std::unique_ptr<imr::Image> depthBuffer;

    auto &vk = device.dispatch;

    // key/id = x*grid_size + z
    std::unordered_map<Ivec2Key, GPUChunk> chunks = {};
    chunks.reserve(NUM_CHUNKS);
    std::vector<ivec2> chunks_to_load = { 
        ivec2(2, 2),
        ivec2(2, 1),
    };

    // stores block data for the whole map!
    std::shared_ptr<imr::Buffer> block_data_buffer = std::make_shared<imr::Buffer>(
        device, 4 * CUNK_SIZE * NUM_CHUNKS,
        VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT);

    push_constants.block_buffer = block_data_buffer->device_address();

    while (!glfwWindowShouldClose(window)) {
        fps_counter.tick();
        camera_update(window, &camera_input);
        camera_move_freelook(&camera, &camera_input, &camera_state, delta);

        transform_matrices.camera_pos = camera.position;

        int offset_x = center_chunk_pos.x - RADIUS;
        int offset_y = center_chunk_pos.y - RADIUS;
                
        // load chunks
        for (int dx = center_chunk_pos.x - RADIUS; dx <= center_chunk_pos.x + RADIUS; dx++)
        for (int dy = center_chunk_pos.y - RADIUS; dy <= center_chunk_pos.y + RADIUS; dy++) 
        // for (ivec2 chunk_pos: chunks_to_load)
        {
            Ivec2Key chunk_pos = Ivec2Key(center_chunk_pos.x + dx, center_chunk_pos.y + dy);
            if (chunks.find(chunk_pos) == chunks.end()) {
                chunks[chunk_pos] = { .location = ivec2(chunk_pos.x, chunk_pos.y) };
            } 

            if (!chunks[chunk_pos].loaded) {
                // chunk id found but blocks not uploaded to buffer
                auto world_chunk = world.get_loaded_chunk(chunk_pos.x, chunk_pos.y);
                if (!world_chunk) {
                        world.load_chunk(chunk_pos.x, chunk_pos.y);
                        std::cout << "Wait chunk to load\n";
                    } 
                    else {
                        // upload chunk
                        uint blocks[CUNK_CHUNK_MAX_HEIGHT*CUNK_CHUNK_SIZE*CUNK_CHUNK_SIZE] = {};
                        for (size_t s = 0; s < CUNK_CHUNK_SECTIONS_COUNT; s++) {
                            if (!world_chunk->data.sections[s]) {
                                continue;
                            }
                            for (size_t y = 0; y < CUNK_CHUNK_SIZE; y++)
                            for (size_t x = 0; x < CUNK_CHUNK_SIZE; x++)
                            for (size_t z = 0; z < CUNK_CHUNK_SIZE; z++) {
                                BlockData b = world_chunk->data.sections[s]->block_data[y][z][x];
                                if (b != BlockAir) {
                                    blocks[(s * CUNK_CHUNK_SIZE + y)*CUNK_HSLICE_SIZE+z*CUNK_CHUNK_SIZE+x] = b;
                                }
                            }
                        }
                    
                        int chunk_index_x = chunk_pos.x - offset_x;
                        int chunk_index_y = chunk_pos.y - offset_y;
                        assert(chunk_index_x >= 0 && chunk_index_y >= 0);
                        assert(chunk_index_x < GRID_SIZE && chunk_index_y < GRID_SIZE);
                        
                        block_data_buffer->uploadDataSync((chunk_index_x * GRID_SIZE + chunk_index_y) * 4 * CUNK_SIZE, 4*CUNK_SIZE, blocks);
                        std::cout << std::format("Chunk at ({}, {}) uploaded\n", chunk_pos.x, chunk_pos.y);
                        chunks[chunk_pos].loaded = true;
                    }
                }
            }
        
        int current_cx = camera.position.x / CUNK_CHUNK_SIZE - int(std::signbit(camera.position.x));
        int current_cz = camera.position.z / CUNK_CHUNK_SIZE - int(std::signbit(camera.position.z));
        Ivec2Key current_chunk_key = Ivec2Key(current_cx, current_cz);

        updateDebugGlfwWindowTitle(window, ivec2(current_cx, current_cz), fps_counter.average_fps());
        bool in_any_chunk = chunks.find(current_chunk_key) != chunks.end() && camera.position.y >= 0 && camera.position.y < CUNK_CHUNK_MAX_HEIGHT;

            swapchain.renderFrameSimplified(
                [&](imr::Swapchain::SimplifiedRenderContext &context) {

                    mat4 m = identity_mat4;
                    mat4 flip_y = identity_mat4;
                    flip_y.rows[1][1] = -1;
                    m = m * flip_y;
                    mat4 view_mat =
                        camera_get_view_mat4(&camera, context.image().size().width,
                                            context.image().size().height);
                    m = m * view_mat;
                    transform_matrices.mpp = m;
                    transform_matrices.mpp_inv = invert_mat4(m);
                            
                    trans_buffer->uploadDataSync(0, trans_buffer->size, &transform_matrices);

                    if (reload_shaders) {
                        swapchain.drain();
                        shaders = std::make_unique<Shaders>(device, swapchain);
                        reload_shaders = false;
                    }

                    auto &image = context.image();
                    auto cmdbuf = context.cmdbuf();

                    if (!depthBuffer ||
                        depthBuffer->size().width != context.image().size().width ||
                        depthBuffer->size().height !=
                            context.image().size().height) {
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
                                .pImageMemoryBarriers = tmpPtr((
                                    VkImageMemoryBarrier2){
                                    .sType =
                                        VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
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
                    

                    auto &pipeline = shaders->pipeline;
                    vkCmdBindPipeline(cmdbuf, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                    pipeline->pipeline());
                    

                    int chunk_id = 1;
                    context.frame().withRenderTargets(
                        cmdbuf, {&image}, &*depthBuffer, [&]() {
                            for (auto chunk : chunks) {
                                if (!chunk.second.loaded) {
                                    continue;
                                }

                                push_constants.chunk = ivec4(
                                    chunk.second.location[0],
                                    chunk.second.location[1],
                                    chunk_id++,
                                    int(in_any_chunk && chunk.first == current_chunk_key));

                                push_constants.chunk_index = (chunk.second.location[0] - offset_x) * GRID_SIZE + (chunk.second.location[1] - offset_y);

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
