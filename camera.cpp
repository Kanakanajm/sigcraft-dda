#include <cassert>

#include "GLFW/glfw3.h"
#include "camera.h"

using namespace nasl;

mat4 camera_rotation_matrix(const Camera *camera) {
    mat4 matrix = identity_mat4;
    matrix = mul_mat4(rotate_axis_mat4(1, camera->rotation.yaw), matrix);
    matrix = mul_mat4(rotate_axis_mat4(0, camera->rotation.pitch), matrix);
    return matrix;
}

mat4 camera_rotation_matrix_axis(const vec3 &axis, float angle) {
    vec3 a = normalize(axis);
    float x = a[0], y = a[1], z = a[2];
    float c = std::cos(angle);
    float s = std::sin(angle);
    float C = 1.0f - c;

    mat4 R = {// Row 0
              c + x * x * C, x * y * C - z * s, x * z * C + y * s, 0.0f,
              // Row 1
              y * x * C + z * s, c + y * y * C, y * z * C - x * s, 0.0f,
              // Row 2
              z * x * C - y * s, z * y * C + x * s, c + z * z * C, 0.0f,
              // Row 3
              0.0f, 0.0f, 0.0f, 1.0f};

    return R;
}

mat4 camera_to_world_rotation_matrix(const Camera *camera) {
    mat4 matrix = identity_mat4;
    matrix = mul_mat4(rotate_axis_mat4(1, -camera->rotation.yaw), matrix);
    vec4 right = {1, 0, 0, 0};
    vec4 right_rotated = matrix * right;
    vec3 right_rotated_axis = normalize(vec3(right_rotated.xyz));
    matrix = mul_mat4(
        camera_rotation_matrix_axis(right_rotated_axis, camera->rotation.pitch),
        matrix);
    return matrix;
}

mat4 camera_get_view_mat4(const Camera *camera, size_t width, size_t height) {
    mat4 matrix = identity_mat4;
    matrix = mul_mat4(translate_mat4(vec3_neg(camera->position)),
                      matrix); // T_translate * I
    matrix = mul_mat4(camera_rotation_matrix(camera),
                      matrix); // T_rotate * T_translate * I
    float ratio = ((float)width) / ((float)height);
    matrix = mul_mat4(perspective_mat4(ratio, camera->fov, 0.1f, 1000.f),
                      matrix); // Project * T_rotate * T_translate * I
    return matrix;
}

mat4 rotate_axis_mat4f(unsigned int axis, float f) {
    mat4 m = {0};
    m.elems.m33 = 1;

    unsigned int t = (axis + 2) % 3;
    unsigned int s = (axis + 1) % 3;

    m.rows[t].arr[t] = cosf(f);
    m.rows[t].arr[s] = -sinf(f);
    m.rows[s].arr[t] = sinf(f);
    m.rows[s].arr[s] = cosf(f);

    // leave that unchanged
    m.rows[axis].arr[axis] = 1;

    return m;
}

vec3 camera_get_forward_vec(const Camera *cam, vec3 forward) {
    vec4 initial_forward(forward, 1);
    // we invert the rotation matrix and use the front vector from the camera
    // space to get the one in world space
    mat4 matrix = camera_to_world_rotation_matrix(cam);
    vec4 result = mul_mat4_vec4f(matrix, initial_forward);
    return vec3_scale(result.xyz, 1.0f / result.w);
}

vec3 camera_get_right_vec(const Camera *cam) {
    vec4 initial_right(1, 0, 0, 1);
    mat4 matrix = camera_to_world_rotation_matrix(cam);
    vec4 result = mul_mat4_vec4f(matrix, initial_right);
    return vec3_scale(result.xyz, 1.0f / result.w);
}

void camera_update(GLFWwindow *handle, CameraInput *input) {
    input->mouse_held =
        glfwGetMouseButton(handle, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;
    glfwGetCursorPos(handle, &input->mouse_x, &input->mouse_y);
    input->keys.forward = glfwGetKey(handle, GLFW_KEY_W) == GLFW_PRESS;
    input->keys.back = glfwGetKey(handle, GLFW_KEY_S) == GLFW_PRESS;
    input->keys.left = glfwGetKey(handle, GLFW_KEY_A) == GLFW_PRESS;
    input->keys.right = glfwGetKey(handle, GLFW_KEY_D) == GLFW_PRESS;
    if (input->should_capture)
        glfwSetInputMode(handle, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
    else
        glfwSetInputMode(handle, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
}

bool camera_move_freelook(Camera *cam, CameraInput *input,
                          CameraFreelookState *state, float delta) {
    assert(cam && input && state);
    bool moved = false;
    if (input->mouse_held) {
        if (state->mouse_was_held) {
            double diff_x = input->mouse_x - state->last_mouse_x;
            double diff_y = input->mouse_y - state->last_mouse_y;
            cam->rotation.yaw += (float)diff_x / (180.0f * (float)M_PI) *
                                 state->mouse_sensitivity;
            cam->rotation.pitch += (float)diff_y / (180.0f * (float)M_PI) *
                                   state->mouse_sensitivity;
            moved = true;
        } else
            input->should_capture = true;

        state->last_mouse_x = input->mouse_x;
        state->last_mouse_y = input->mouse_y;
    } else
        input->should_capture = false;
    state->mouse_was_held = input->mouse_held;

    if (input->keys.forward) {
        cam->position =
            vec3_add(cam->position, vec3_scale(camera_get_forward_vec(cam),
                                               state->fly_speed * delta));
        moved = true;
    } else if (input->keys.back) {
        cam->position =
            vec3_sub(cam->position, vec3_scale(camera_get_forward_vec(cam),
                                               state->fly_speed * delta));
        moved = true;
    }

    if (input->keys.right) {
        cam->position =
            vec3_add(cam->position, vec3_scale(camera_get_right_vec(cam),
                                               state->fly_speed * delta));
        moved = true;
    } else if (input->keys.left) {
        cam->position =
            vec3_sub(cam->position, vec3_scale(camera_get_right_vec(cam),
                                               state->fly_speed * delta));
        moved = true;
    }
    return moved;
}

namespace smath {

float dot(const vec3 &a, const vec3 &b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

vec3 cross(const vec3 &a, const vec3 &b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z,
            a.x * b.y - a.y * b.x};
}

vec3 normalize(const vec3 &v) {
    float l = sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
    if (l == 0.0f)
        return {0.0f, 0.0f, 0.0f};
    return {v.x / l, v.y / l, v.z / l};
}

vec3 rotateY(const vec3 &v, float angleRadians) {
    float c = cosf(angleRadians);
    float s = sinf(angleRadians);

    vec3 result;
    result.x = v.x * c + v.z * s;
    result.y = v.y;
    result.z = -v.x * s + v.z * c;
    return result;
}

vec3 rotateAroundAxis(const vec3 &v, const vec3 &axis, float angle) {
    float c = cosf(angle);
    float s = sinf(angle);

    return c * v + s * cross(axis, v) + dot(axis, v) * (1 - c) * axis;
}

} // namespace smath

Plane camera_get_plane(const Camera *cam) {
    vec3 forward = {0, 0, -1};
    vec3 world_up = {0, 1, 0};

    forward = smath::normalize(smath::rotateY(forward, cam->rotation.yaw));
    vec3 right_after_yaw = smath::normalize(smath::cross(forward, world_up));
    forward = smath::normalize(
        smath::rotateAroundAxis(forward, right_after_yaw, cam->rotation.pitch));

    vec3 right = smath::normalize(smath::cross(forward, world_up));
    vec3 up = smath::normalize(smath::cross(right, forward));

    return {forward, right, up};
}
