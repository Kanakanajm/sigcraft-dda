#version 450
#extension GL_EXT_shader_image_load_formatted : require
#extension GL_EXT_scalar_block_layout : require
#extension GL_EXT_buffer_reference : require

layout(set = 0, binding = 0) uniform image2D renderTarget;

layout(local_size_x = 32, local_size_y = 32, local_size_z = 1) in;

layout(scalar, buffer_reference) buffer MapBuffer { int map[4][4][4]; };
layout(scalar, push_constant) uniform T {
  MapBuffer map_buffer;
  vec3 pos;
  vec3 dir;
  vec2 planeX;
  vec2 planeY;
}
push_constants;

void main() {
  ivec2 img_size = imageSize(renderTarget);

  // block on map
  int mapX = int(push_constants.pos.x);
  int mapY = int(push_constants.pos.y);

  // x-coordinate in camera space, in [-1, 1]
  float cameraX = 2 * gl_GlobalInvocationID.x / float(img_size.x) - 1;

  vec2 ray = push_constants.dir + push_constants.plane * cameraX;

  float deltaDistX = ray.x == 0 ? 1e30 : abs(1 / ray.x);
  float deltaDistY = ray.y == 0 ? 1e30 : abs(1 / ray.y);

  // length of ray from current position to next x or y-side
  float sideDistX;
  float sideDistY;

  // distance to the wall
  float perpWallDist;

  // what direction to step in x or y-direction (either +1 or -1)
  int stepX;
  int stepY;
  int hit = 0; // was there a wall hit?
  int side;    // was a NS or a EW wall hit?

  if (ray.x < 0) {
    stepX = -1;
    sideDistX = (push_constants.pos.x - mapX) * deltaDistX;
  } else {
    stepX = 1;
    sideDistX = (mapX + 1.0 - push_constants.pos.x) * deltaDistX;
  }
  if (ray.y < 0) {
    stepY = -1;
    sideDistY = (push_constants.pos.y - mapY) * deltaDistY;
  } else {
    stepY = 1;
    sideDistY = (mapY + 1.0 - push_constants.pos.y) * deltaDistY;
  }
  // perform DDA
  int i = 0;
  while (hit == 0 && i < 100) {
    // jump to next map square, either in x-direction, or in y-direction
    if (sideDistX < sideDistY) {
      sideDistX += deltaDistX;
      mapX += stepX;
      side = 0;
    } else {
      sideDistY += deltaDistY;
      mapY += stepY;
      side = 1;
    }
    // Check if ray has hit a wall
    if (push_constants.map_buffer.map[mapX][mapY] > 0)
      hit = 1;
    i++;
  }
  // Calculate distance projected on camera direction (Euclidean distance would
  // give fisheye effect!)
  if (side == 0)
    perpWallDist = sideDistX - deltaDistX;
  else
    perpWallDist = sideDistY - deltaDistY;

  // Calculate height of line to draw on screen
  int lineHeight = int(img_size.y / perpWallDist);

  // //calculate lowest and highest pixel to fill in current stripe
  int drawStart = -lineHeight / 2 + img_size.y / 2;
  if (drawStart < 0)
    drawStart = 0;
  int drawEnd = lineHeight / 2 + img_size.y / 2;
  if (drawEnd >= img_size.y)
    drawEnd = img_size.y - 1;

  vec4 c = vec4(0.0, 0.0, 0.0, 1.0);

  if (gl_GlobalInvocationID.y <= drawEnd &&
      gl_GlobalInvocationID.y >= drawStart) {
    switch (push_constants.map_buffer.map[mapX][mapY]) {
    case 1:
      c = vec4(0.5, 0.5, 0.5, 1.0);
      break; // grey
    case 2:
      c = vec4(0.0, 1.0, 0.0, 1.0);
      break; // green
    case 3:
      c = vec4(0.0, 0.0, 1.0, 1.0);
      break; // blue
    case 4:
      c = vec4(1.0, 1.0, 1.0, 1.0);
      break; // white
    case 5:
      c = vec4(1.0, 0.0, 0.0, 1.0);
      break; // red
    default:
      c = vec4(0.0, 0.0, 0.0, 1.0);
      break; // black
    }
    // give x and y sides different brightness
    if (side == 1) {
      c = c / 2;
    }
  }
  imageStore(renderTarget, ivec2(gl_GlobalInvocationID.xy), c);
}