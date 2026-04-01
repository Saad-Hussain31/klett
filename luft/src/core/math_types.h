#pragma once

#include <cmath>
#include <cstdint>
#include <algorithm>

namespace luft
{

    // ──────────────────────────────────────────────
    // Constants
    // ──────────────────────────────────────────────

    inline constexpr double kPi = 3.14159265358979323846;
    inline constexpr double kDegToRad = kPi / 180.0;
    inline constexpr double kRadToDeg = 180.0 / kPi;
    inline constexpr double kGravity = 9.80665;            // m/s^2, standard gravity
    inline constexpr double kSeaLevelPressure = 101325.0;  // Pa
    inline constexpr double kSeaLevelTemperature = 288.15; // K (15 C)
    inline constexpr double kSeaLevelDensity = 1.225;      // kg/m^3
    inline constexpr double kLapseRate = 0.0065;           // K/m (troposphere)
    inline constexpr double kTropopauseAlt = 11000.0;      // m
    inline constexpr double kGasConstantAir = 287.058;     // J/(kg*K)
    inline constexpr double kGammaAir = 1.4;               // ratio of specific heats

    // ──────────────────────────────────────────────
    // Vec3 — 3D vector, used for position, velocity, force, etc.
    // ──────────────────────────────────────────────

    struct Vec3
    {
        double x = 0.0;
        double y = 0.0;
        double z = 0.0;

        constexpr Vec3() = default;
        constexpr Vec3(double x_, double y_, double z_) : x(x_), y(y_), z(z_) {}

        constexpr Vec3 operator+(const Vec3 &o) const { return {x + o.x, y + o.y, z + o.z}; }
        constexpr Vec3 operator-(const Vec3 &o) const { return {x - o.x, y - o.y, z - o.z}; }
        constexpr Vec3 operator*(double s) const { return {x * s, y * s, z * s}; }
        constexpr Vec3 operator/(double s) const { return {x / s, y / s, z / s}; }
        constexpr Vec3 &operator+=(const Vec3 &o)
        {
            x += o.x;
            y += o.y;
            z += o.z;
            return *this;
        }
        constexpr Vec3 &operator-=(const Vec3 &o)
        {
            x -= o.x;
            y -= o.y;
            z -= o.z;
            return *this;
        }
        constexpr Vec3 &operator*=(double s)
        {
            x *= s;
            y *= s;
            z *= s;
            return *this;
        }

        [[nodiscard]] double magnitude() const { return std::sqrt(x * x + y * y + z * z); }
        [[nodiscard]] constexpr double magnitude_sq() const { return x * x + y * y + z * z; }
        [[nodiscard]] constexpr double dot(const Vec3 &o) const { return x * o.x + y * o.y + z * o.z; }
        [[nodiscard]] constexpr Vec3 cross(const Vec3 &o) const
        {
            return {y * o.z - z * o.y, z * o.x - x * o.z, x * o.y - y * o.x};
        }
        [[nodiscard]] Vec3 normalized() const
        {
            double m = magnitude();
            if (m < 1e-12)
                return {0.0, 0.0, 0.0};
            return *this / m;
        }

        static constexpr Vec3 zero() { return {0.0, 0.0, 0.0}; }
    };

    constexpr Vec3 operator*(double s, const Vec3 &v) { return v * s; }

    // ──────────────────────────────────────────────
    // Quaternion — orientation representation (avoids gimbal lock)
    // Convention: w is scalar part, (x,y,z) is vector part
    // ──────────────────────────────────────────────

    struct Quaternion
    {
        double w = 1.0;
        double x = 0.0;
        double y = 0.0;
        double z = 0.0;

        constexpr Quaternion() = default;
        constexpr Quaternion(double w_, double x_, double y_, double z_) : w(w_), x(x_), y(y_), z(z_) {}

        [[nodiscard]] Quaternion operator*(const Quaternion &q) const
        {
            return {
                w * q.w - x * q.x - y * q.y - z * q.z,
                w * q.x + x * q.w + y * q.z - z * q.y,
                w * q.y - x * q.z + y * q.w + z * q.x,
                w * q.z + x * q.y - y * q.x + z * q.w};
        }

        [[nodiscard]] double norm() const { return std::sqrt(w * w + x * x + y * y + z * z); }

        [[nodiscard]] Quaternion normalized() const
        {
            double n = norm();
            if (n < 1e-12)
                return {1.0, 0.0, 0.0, 0.0};
            return {w / n, x / n, y / n, z / n};
        }

        [[nodiscard]] constexpr Quaternion conjugate() const { return {w, -x, -y, -z}; }

        // Rotate a body-frame vector to the world frame
        [[nodiscard]] Vec3 rotate(const Vec3 &v) const
        {
            Quaternion qv{0.0, v.x, v.y, v.z};
            Quaternion result = (*this) * qv * conjugate();
            return {result.x, result.y, result.z};
        }

        // Rotate a world-frame vector to the body frame
        [[nodiscard]] Vec3 inverse_rotate(const Vec3 &v) const
        {
            Quaternion qv{0.0, v.x, v.y, v.z};
            Quaternion result = conjugate() * qv * (*this);
            return {result.x, result.y, result.z};
        }

        // Quaternion derivative from angular velocity (body frame)
        // dq/dt = 0.5 * q * omega_quat
        [[nodiscard]] Quaternion derivative(const Vec3 &omega) const
        {
            Quaternion omega_q{0.0, omega.x, omega.y, omega.z};
            Quaternion dq = (*this) * omega_q;
            return {dq.w * 0.5, dq.x * 0.5, dq.y * 0.5, dq.z * 0.5};
        }

        [[nodiscard]] Quaternion operator+(const Quaternion &q) const
        {
            return {w + q.w, x + q.x, y + q.y, z + q.z};
        }

        [[nodiscard]] Quaternion operator*(double s) const
        {
            return {w * s, x * s, y * s, z * s};
        }

        // Extract Euler angles (roll, pitch, yaw) in radians
        [[nodiscard]] Vec3 to_euler() const
        {
            double sinr_cosp = 2.0 * (w * x + y * z);
            double cosr_cosp = 1.0 - 2.0 * (x * x + y * y);
            double roll = std::atan2(sinr_cosp, cosr_cosp);

            double sinp = 2.0 * (w * y - z * x);
            double pitch;
            if (std::abs(sinp) >= 1.0)
                pitch = std::copysign(kPi / 2.0, sinp);
            else
                pitch = std::asin(sinp);

            double siny_cosp = 2.0 * (w * z + x * y);
            double cosy_cosp = 1.0 - 2.0 * (y * y + z * z);
            double yaw = std::atan2(siny_cosp, cosy_cosp);

            return {roll, pitch, yaw};
        }

        // Construct from Euler angles (roll, pitch, yaw) in radians
        [[nodiscard]] static Quaternion from_euler(double roll, double pitch, double yaw)
        {
            double cr = std::cos(roll * 0.5), sr = std::sin(roll * 0.5);
            double cp = std::cos(pitch * 0.5), sp = std::sin(pitch * 0.5);
            double cy = std::cos(yaw * 0.5), sy = std::sin(yaw * 0.5);
            return {
                cr * cp * cy + sr * sp * sy,
                sr * cp * cy - cr * sp * sy,
                cr * sp * cy + sr * cp * sy,
                cr * cp * sy - sr * sp * cy};
        }

        static constexpr Quaternion identity() { return {1.0, 0.0, 0.0, 0.0}; }
    };

    // ──────────────────────────────────────────────
    // Utility functions
    // ──────────────────────────────────────────────

    inline constexpr double clamp(double val, double lo, double hi)
    {
        return (val < lo) ? lo : (val > hi) ? hi
                                            : val;
    }

    inline constexpr double lerp(double a, double b, double t)
    {
        return a + t * (b - a);
    }

} // namespace luft
