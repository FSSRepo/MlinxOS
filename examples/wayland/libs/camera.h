#ifndef CAMERA_H
#define CAMERA_H

#include "ogl_math.h"

enum Camera_Movement {
    FORWARD,
    BACKWARD,
    LEFT,
    RIGHT
};

class Camera {
public:
    Vec3 position;
    Vec3 front;
    Vec3 up;
    Vec3 right;
    Vec3 world_up;
    float yaw;
    float pitch;
    float movement_speed;
    float mouse_sensitivity;
    float zoom;

    Camera(Vec3 position = Vec3(0.0f, 0.0f, 0.0f), Vec3 up = Vec3(0.0f, 1.0f, 0.0f), float yaw = -90.0f, float pitch = 0.0f)
        : front(Vec3(0.0f, 0.0f, -1.0f)), movement_speed(2.5f), mouse_sensitivity(0.1f), zoom(45.0f) {
        this->position = position;
        this->world_up = up;
        this->yaw = yaw;
        this->pitch = pitch;
        update_vectors();
    }

    Mat4 get_view_matrix() const {
        return Mat4::lookAt(position, position + front, up);
    }

    void process_keyboard(Camera_Movement direction, float delta_time) {
        float velocity = movement_speed * delta_time;
        if (direction == FORWARD) position = position + front * velocity;
        if (direction == BACKWARD) position = position - front * velocity;
        if (direction == LEFT) position = position - right * velocity;
        if (direction == RIGHT) position = position + right * velocity;
    }

    void process_mouse(float xoffset, float yoffset, bool constrain_pitch = true) {
        xoffset *= mouse_sensitivity;
        yoffset *= mouse_sensitivity;

        yaw += xoffset;
        pitch += yoffset;

        if (constrain_pitch) {
            if (pitch > 89.0f) pitch = 89.0f;
            if (pitch < -89.0f) pitch = -89.0f;
        }

        update_vectors();
    }

private:
    void update_vectors() {
        Vec3 f;
        f.x = std::cos(yaw * (float)M_PI / 180.0f) * std::cos(pitch * (float)M_PI / 180.0f);
        f.y = std::sin(pitch * (float)M_PI / 180.0f);
        f.z = std::sin(yaw * (float)M_PI / 180.0f) * std::cos(pitch * (float)M_PI / 180.0f);
        front = f.normalize();
        right = front.cross(world_up).normalize();
        up = right.cross(front).normalize();
    }
};

#endif
