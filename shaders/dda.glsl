#version 450
#extension GL_EXT_shader_image_load_formatted : require
#extension GL_EXT_scalar_block_layout : require
#extension GL_EXT_buffer_reference : require

#define M_PI 3.14159265358979323846
#define VISIBILITY 100 // maximum number of blocks to traverse in DDA

layout(set = 0, binding = 0) uniform image2D renderTarget;

layout(local_size_x = 32, local_size_y = 32, local_size_z = 1) in;

layout(scalar, buffer_reference) buffer ChunkBuffer { int data[16][384][16]; };
layout(scalar, push_constant) uniform T {
  ChunkBuffer chunk_buffer;
  ivec2 chunk_pos; // .x = cx, .y = cz
  mat4 m;
  float fov;
  vec3[14] color_palette;
}
push_constants;

void main() {
  ivec2 img_size = imageSize(renderTarget);
  float img_aspect_ratio = img_size.x / float(img_size.y);

  // spawn camera
  vec4 camera_origin = vec4(0, 0, 0, 1);
  float px = (2 * ((gl_GlobalInvocationID.x + 0.5) / img_size.x) - 1) *
             tan(push_constants.fov / 2 * M_PI / 180) * img_aspect_ratio;
  float py = (1 - 2 * ((gl_GlobalInvocationID.y + 0.5) / img_size.y)) *
             tan(push_constants.fov / 2 * M_PI / 180);
  vec3 camera_dir_ = normalize(vec3(px, py, -1));
  vec4 camera_dir = vec4(camera_dir_.x, camera_dir_.y, camera_dir_.z, 0);

  // camera space to world space
  camera_dir = push_constants.m * camera_dir;
  camera_origin = push_constants.m * camera_origin;

  // world space to chunk space
  camera_origin.x -= push_constants.chunk_pos.x;
  camera_origin.z -= push_constants.chunk_pos.y;

  // vec3 camera_origin = vec3(16, 384, 16);
  // vec3 dir = vec3(0, -1, 0);

  // vec3 planeX = vec3(-12, 0, 0);
  // vec3 planeY = vec3(0, 0, -8);
  // float cameraX = 2 * gl_GlobalInvocationID.x / float(img_size.x) - 1;
  // float cameraY = 2 * gl_GlobalInvocationID.y / float(img_size.y) - 1;

  // vec3 camera_dir = dir + planeX * cameraX + planeY * cameraY;

  // block on map
  int mapX = int(camera_origin.x);
  int mapY = int(camera_origin.y);
  int mapZ = int(camera_origin.z);

  float deltaDistX = camera_dir.x == 0 ? 1e30 : abs(1 / camera_dir.x);
  float deltaDistY = camera_dir.y == 0 ? 1e30 : abs(1 / camera_dir.y);
  float deltaDistZ = camera_dir.z == 0 ? 1e30 : abs(1 / camera_dir.z);

  // length of ray from current position to next x, y or z-side
  float sideDistX;
  float sideDistY;
  float sideDistZ;

  // what direction to step in x, y or z-direction (either +1 or -1)
  int stepX;
  int stepY;
  int stepZ;
  int hit = 0; // was there a wall hit?
  int side;    // was a NS(1), a EW(0) or a FB(2) wall hit?

  // init step and sideDist
  if (camera_dir.x < 0) {
    stepX = -1;
    sideDistX = (camera_origin.x - mapX) * deltaDistX;
  } else {
    stepX = 1;
    sideDistX = (mapX + 1.0 - camera_origin.x) * deltaDistX;
  }

  if (camera_dir.y < 0) {
    stepY = -1;
    sideDistY = (camera_origin.y - mapY) * deltaDistY;
  } else {
    stepY = 1;
    sideDistY = (mapY + 1.0 - camera_origin.y) * deltaDistY;
  }

  if (camera_dir.z < 0) {
    stepZ = -1;
    sideDistZ = (camera_origin.z - mapZ) * deltaDistZ;
  } else {
    stepZ = 1;
    sideDistZ = (mapZ + 1.0 - camera_origin.z) * deltaDistZ;
  }

  // perform DDA
  int i = 0;
  int block_type = 0;
  while (hit == 0 && i < VISIBILITY) {
    // jump to next map square, either in x, y or z-direction
    if (sideDistX < sideDistY) {
      if (sideDistX < sideDistZ) {
        sideDistX += deltaDistX;
        mapX += stepX;
        side = 0;
      } else {
        sideDistZ += deltaDistZ;
        mapZ += stepZ;
        side = 2;
      }
    } else {
      if (sideDistY < sideDistZ) {
        sideDistY += deltaDistY;
        mapY += stepY;
        side = 1;
      } else {
        sideDistZ += deltaDistZ;
        mapZ += stepZ;
        side = 2;
      }
    }

    // Check if ray has hit a wall
    if (mapX >= 0 && mapX < 16 && mapZ >= 0 && mapZ < 16 && mapY >= 0 && mapY < 384 ) {
      if (push_constants.chunk_buffer.data[mapX][mapY][mapZ] > 0) {
        hit = 1;
        block_type = push_constants.chunk_buffer.data[mapX][mapY][mapZ];
      }
    }
    i++;
  }

  vec4 c = vec4(0.0, 0.0, 0.0, 1.0);

  // if (hit == 1) {
  //   c = vec4(1.0, 1.0, 1.0, 1.0);
  // }

  if (block_type > 0 && block_type < 14) {
    // color known block type

    c.x = push_constants.color_palette[block_type].x;
    c.y = push_constants.color_palette[block_type].y;
    c.z = push_constants.color_palette[block_type].z;
  }

  imageStore(renderTarget, ivec2(gl_GlobalInvocationID.xy), c);
}