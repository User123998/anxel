#pragma once

#include "world.hpp"
#include <algorithm>
#include <cmath>

namespace umbra {

class Camera {
public:
    vec3  position{0.0f, 36.0f, -6.0f};
    vec3  velocity{0.0f};
    float yaw      = 90.0f; // Facing +Z across the valley & landmark cavern
    float pitch    = -8.0f;
    float fov      = 70.0f;
    bool  flyMode         = true;
    bool  onGround        = false;
    bool  inWater         = false;
    bool  eyeUnderwater   = false;
    int   waterDepthAbove = 0;
    float waterDepthExact = 0.0f;

    vec3 forward() const noexcept {
        float yr = radians(yaw);
        float pr = radians(pitch);
        return normalize(vec3(
            std::cos(yr) * std::cos(pr),
            std::sin(pr),
            std::sin(yr) * std::cos(pr)
        ));
    }

    vec3 right() const noexcept {
        return normalize(cross(forward(), vec3(0.0f, 1.0f, 0.0f)));
    }

    mat4 viewMatrix() const noexcept {
        return lookAt(position, position + forward(), vec3(0.0f, 1.0f, 0.0f));
    }

    // Translation-free rotation-only view matrix so sky, Sun, Moon & stars NEVER jitter or vibrate
    // regardless of camera XYZ coordinates or movement speed!
    mat4 viewMatrixRotationOnly() const noexcept {
        return lookAt(vec3(0.0f, 0.0f, 0.0f), forward(), vec3(0.0f, 1.0f, 0.0f));
    }

    // Vulkan clip space: depth [0, 1] + Y-axis flip built directly into pure C++20 projection
    // Far plane scales dynamically with Render Distance (up to 50 chunks = 800+ blocks!)
    mat4 projectionMatrix(float aspect, float farPlane = 420.0f) const noexcept {
        return perspectiveVulkan(radians(fov), std::max(aspect, 0.1f), 0.12f, std::max(farPlane, 220.0f));
    }

    void rotate(float dx, float dy, float sensitivity = 0.12f) noexcept {
        yaw   += dx * sensitivity;
        pitch -= dy * sensitivity;
        pitch  = std::clamp(pitch, -88.5f, 88.5f);
    }

    // Updates camera water immersion status (used for depth-dependent underwater optics + swimming buoyancy)
    void refreshWaterState(const VoxelWorld& world) noexcept {
        int ex = fastFloor(position.x);
        int ey = fastFloor(position.y);
        int ez = fastFloor(position.z);
        BlockType eyeBlock  = world.getBlock(ex, ey, ez);
        BlockType bodyBlock = world.getBlock(ex, fastFloor(position.y - 0.85f), ez);

        // Account for the recessed water surface at y + 0.88 so the eye smoothly crosses the meniscus
        bool underMeniscus = false;
        if (eyeBlock == BlockType::Water) {
            BlockType above = world.getBlock(ex, ey + 1, ez);
            float localY = position.y - static_cast<float>(ey);
            underMeniscus = (above == BlockType::Water) || (localY < 0.86f);
        }

        eyeUnderwater = underMeniscus;
        inWater       = underMeniscus || (bodyBlock == BlockType::Water);

        int depth = 0;
        float exactDepth = 0.0f;
        if (eyeUnderwater) {
            for (int y = ey; y < CHUNK_HEIGHT && depth < 64; ++y) {
                if (world.getBlock(ex, y, ez) == BlockType::Water) ++depth;
                else break;
            }
            if (depth > 0) {
                int topWaterY = ey + depth - 1;
                BlockType aboveTop = world.getBlock(ex, topWaterY + 1, ez);
                bool openSurface = (aboveTop == BlockType::Air ||
                                    aboveTop == BlockType::Torch ||
                                    aboveTop == BlockType::Leaves);
                float surfaceY = static_cast<float>(topWaterY) + (openSurface ? 0.88f : 1.0f);
                exactDepth = std::max(0.0f, surfaceY - position.y);
            }
        }
        waterDepthAbove = depth;
        waterDepthExact = exactDepth;
    }

    // Unclamped infinite X/Z traversal with optional gravity walk & buoyant swimming collision
    void update(float dt, const VoxelWorld& world,
                float moveFwd, float moveRight, float moveUp, bool jumpPressed, bool sprint) {
        vec3 fwd = forward();
        vec3 flatFwd = normalize(vec3(fwd.x, 0.0f, fwd.z));
        vec3 flatRight = right();
        vec3 wish = flatFwd * moveFwd + flatRight * moveRight;
        if (lengthSq(wish) > 1.0f) wish = normalize(wish);

        refreshWaterState(world);

        if (flyMode) {
            float speed = sprint ? 42.0f : 15.0f;
            position += (wish + vec3(0.0f, moveUp, 0.0f)) * (speed * dt);
            position.y = std::clamp(position.y, 2.0f, static_cast<float>(CHUNK_HEIGHT) + 64.0f);
            velocity = vec3(0.0f);
            refreshWaterState(world);
            return;
        }

        if (inWater) {
            // Smooth buoyant swimming: gentle drag, upward stroke on Space, dive on Shift/Ctrl
            float swimSpeed = sprint ? 6.2f : 4.2f;
            vec3 targetVel = wish * swimSpeed;
            float drag = std::min(1.0f, dt * 6.5f);
            velocity.x += (targetVel.x - velocity.x) * drag;
            velocity.z += (targetVel.z - velocity.z) * drag;

            if (moveUp > 0.1f || jumpPressed) {
                velocity.y += (5.2f - velocity.y) * std::min(1.0f, dt * 7.0f);
            } else if (moveUp < -0.1f) {
                velocity.y += (-4.6f - velocity.y) * std::min(1.0f, dt * 7.0f);
            } else {
                // Gentle natural sinking when idling underwater
                velocity.y += (-1.2f - velocity.y) * std::min(1.0f, dt * 4.5f);
            }
        } else {
            constexpr float WALK_SPEED = 6.5f;
            constexpr float GRAVITY    = -22.0f;
            constexpr float JUMP_VEL   = 8.6f;

            float speed = sprint ? 10.0f : WALK_SPEED;
            velocity.x = wish.x * speed;
            velocity.z = wish.z * speed;
            if (jumpPressed && onGround) {
                velocity.y = JUMP_VEL;
                onGround = false;
            }
            velocity.y = std::max(velocity.y + GRAVITY * dt, -28.0f);
        }

        moveAndCollide(world, vec3(velocity.x * dt, 0.0f, 0.0f));
        moveAndCollide(world, vec3(0.0f, 0.0f, velocity.z * dt));
        onGround = false;
        if (moveAndCollide(world, vec3(0.0f, velocity.y * dt, 0.0f))) {
            if (velocity.y < 0.0f) onGround = true;
            velocity.y = 0.0f;
        }
        refreshWaterState(world);
    }

private:
    static constexpr float RADIUS = 0.30f;
    static constexpr float EYE_H  = 1.55f;
    static constexpr float HEAD_H = 0.20f;

    bool collides(const VoxelWorld& world, const vec3& eyePos) const {
        int minX = static_cast<int>(std::floor(eyePos.x - RADIUS));
        int maxX = static_cast<int>(std::floor(eyePos.x + RADIUS));
        int minY = static_cast<int>(std::floor(eyePos.y - EYE_H));
        int maxY = static_cast<int>(std::floor(eyePos.y + HEAD_H));
        int minZ = static_cast<int>(std::floor(eyePos.z - RADIUS));
        int maxZ = static_cast<int>(std::floor(eyePos.z + RADIUS));

        for (int y = minY; y <= maxY; ++y) {
            for (int z = minZ; z <= maxZ; ++z) {
                for (int x = minX; x <= maxX; ++x) {
                    if (isSolidCollider(world.getBlock(x, y, z))) return true;
                }
            }
        }
        return false;
    }

    bool moveAndCollide(const VoxelWorld& world, const vec3& delta) {
        vec3 nextPos = position + delta;
        if (!collides(world, nextPos)) {
            position = nextPos;
            return false;
        }
        return true;
    }
};

} // namespace umbra
