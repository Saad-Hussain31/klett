#include <gtest/gtest.h>
#include "math_types.h"
#include <cmath>

using namespace luft;

// ── Vec3 Tests ──────────────────────────────────

TEST(Vec3, DefaultConstructorIsZero)
{
    Vec3 v;
    EXPECT_DOUBLE_EQ(v.x, 0.0);
    EXPECT_DOUBLE_EQ(v.y, 0.0);
    EXPECT_DOUBLE_EQ(v.z, 0.0);
}

TEST(Vec3, ParameterizedConstructor)
{
    Vec3 v(1.0, 2.0, 3.0);
    EXPECT_DOUBLE_EQ(v.x, 1.0);
    EXPECT_DOUBLE_EQ(v.y, 2.0);
    EXPECT_DOUBLE_EQ(v.z, 3.0);
}

TEST(Vec3, Addition)
{
    Vec3 a(1.0, 2.0, 3.0);
    Vec3 b(4.0, 5.0, 6.0);
    Vec3 c = a + b;
    EXPECT_DOUBLE_EQ(c.x, 5.0);
    EXPECT_DOUBLE_EQ(c.y, 7.0);
    EXPECT_DOUBLE_EQ(c.z, 9.0);
}

TEST(Vec3, Subtraction)
{
    Vec3 a(5.0, 7.0, 9.0);
    Vec3 b(1.0, 2.0, 3.0);
    Vec3 c = a - b;
    EXPECT_DOUBLE_EQ(c.x, 4.0);
    EXPECT_DOUBLE_EQ(c.y, 5.0);
    EXPECT_DOUBLE_EQ(c.z, 6.0);
}

TEST(Vec3, ScalarMultiply)
{
    Vec3 v(1.0, 2.0, 3.0);
    Vec3 r = v * 3.0;
    EXPECT_DOUBLE_EQ(r.x, 3.0);
    EXPECT_DOUBLE_EQ(r.y, 6.0);
    EXPECT_DOUBLE_EQ(r.z, 9.0);
}

TEST(Vec3, ScalarMultiplyLeftSide)
{
    Vec3 v(1.0, 2.0, 3.0);
    Vec3 r = 3.0 * v;
    EXPECT_DOUBLE_EQ(r.x, 3.0);
    EXPECT_DOUBLE_EQ(r.y, 6.0);
    EXPECT_DOUBLE_EQ(r.z, 9.0);
}

TEST(Vec3, ScalarDivide)
{
    Vec3 v(6.0, 9.0, 12.0);
    Vec3 r = v / 3.0;
    EXPECT_DOUBLE_EQ(r.x, 2.0);
    EXPECT_DOUBLE_EQ(r.y, 3.0);
    EXPECT_DOUBLE_EQ(r.z, 4.0);
}

TEST(Vec3, DotProduct)
{
    Vec3 a(1.0, 2.0, 3.0);
    Vec3 b(4.0, 5.0, 6.0);
    EXPECT_DOUBLE_EQ(a.dot(b), 32.0); // 4+10+18
}

TEST(Vec3, DotProductOrthogonal)
{
    Vec3 a(1.0, 0.0, 0.0);
    Vec3 b(0.0, 1.0, 0.0);
    EXPECT_DOUBLE_EQ(a.dot(b), 0.0);
}

TEST(Vec3, CrossProduct)
{
    Vec3 a(1.0, 0.0, 0.0);
    Vec3 b(0.0, 1.0, 0.0);
    Vec3 c = a.cross(b);
    EXPECT_DOUBLE_EQ(c.x, 0.0);
    EXPECT_DOUBLE_EQ(c.y, 0.0);
    EXPECT_DOUBLE_EQ(c.z, 1.0);
}

TEST(Vec3, CrossProductAnticommutative)
{
    Vec3 a(1.0, 2.0, 3.0);
    Vec3 b(4.0, 5.0, 6.0);
    Vec3 axb = a.cross(b);
    Vec3 bxa = b.cross(a);
    EXPECT_DOUBLE_EQ(axb.x, -bxa.x);
    EXPECT_DOUBLE_EQ(axb.y, -bxa.y);
    EXPECT_DOUBLE_EQ(axb.z, -bxa.z);
}

TEST(Vec3, Magnitude)
{
    Vec3 v(3.0, 4.0, 0.0);
    EXPECT_DOUBLE_EQ(v.magnitude(), 5.0);
}

TEST(Vec3, MagnitudeSq)
{
    Vec3 v(1.0, 2.0, 3.0);
    EXPECT_DOUBLE_EQ(v.magnitude_sq(), 14.0);
}

TEST(Vec3, Normalization)
{
    Vec3 v(3.0, 4.0, 0.0);
    Vec3 n = v.normalized();
    EXPECT_NEAR(n.x, 0.6, 1e-12);
    EXPECT_NEAR(n.y, 0.8, 1e-12);
    EXPECT_NEAR(n.z, 0.0, 1e-12);
    EXPECT_NEAR(n.magnitude(), 1.0, 1e-12);
}

TEST(Vec3, NormalizationOfZeroVector)
{
    Vec3 v = Vec3::zero();
    Vec3 n = v.normalized();
    EXPECT_DOUBLE_EQ(n.x, 0.0);
    EXPECT_DOUBLE_EQ(n.y, 0.0);
    EXPECT_DOUBLE_EQ(n.z, 0.0);
}

TEST(Vec3, ZeroVector)
{
    Vec3 v = Vec3::zero();
    EXPECT_DOUBLE_EQ(v.x, 0.0);
    EXPECT_DOUBLE_EQ(v.y, 0.0);
    EXPECT_DOUBLE_EQ(v.z, 0.0);
    EXPECT_DOUBLE_EQ(v.magnitude(), 0.0);
}

TEST(Vec3, CompoundAssignmentAdd)
{
    Vec3 a(1.0, 2.0, 3.0);
    a += Vec3(4.0, 5.0, 6.0);
    EXPECT_DOUBLE_EQ(a.x, 5.0);
    EXPECT_DOUBLE_EQ(a.y, 7.0);
    EXPECT_DOUBLE_EQ(a.z, 9.0);
}

TEST(Vec3, CompoundAssignmentSub)
{
    Vec3 a(5.0, 7.0, 9.0);
    a -= Vec3(1.0, 2.0, 3.0);
    EXPECT_DOUBLE_EQ(a.x, 4.0);
    EXPECT_DOUBLE_EQ(a.y, 5.0);
    EXPECT_DOUBLE_EQ(a.z, 6.0);
}

TEST(Vec3, CompoundAssignmentMul)
{
    Vec3 a(1.0, 2.0, 3.0);
    a *= 2.0;
    EXPECT_DOUBLE_EQ(a.x, 2.0);
    EXPECT_DOUBLE_EQ(a.y, 4.0);
    EXPECT_DOUBLE_EQ(a.z, 6.0);
}

// ── Quaternion Tests ────────────────────────────

TEST(Quaternion, Identity)
{
    Quaternion q = Quaternion::identity();
    EXPECT_DOUBLE_EQ(q.w, 1.0);
    EXPECT_DOUBLE_EQ(q.x, 0.0);
    EXPECT_DOUBLE_EQ(q.y, 0.0);
    EXPECT_DOUBLE_EQ(q.z, 0.0);
}

TEST(Quaternion, DefaultIsIdentity)
{
    Quaternion q;
    EXPECT_DOUBLE_EQ(q.w, 1.0);
    EXPECT_DOUBLE_EQ(q.x, 0.0);
    EXPECT_DOUBLE_EQ(q.y, 0.0);
    EXPECT_DOUBLE_EQ(q.z, 0.0);
}

TEST(Quaternion, Norm)
{
    Quaternion q = Quaternion::identity();
    EXPECT_DOUBLE_EQ(q.norm(), 1.0);
}

TEST(Quaternion, MultiplicationWithIdentity)
{
    Quaternion q(0.5, 0.5, 0.5, 0.5);
    Quaternion id = Quaternion::identity();
    Quaternion r = q * id;
    EXPECT_NEAR(r.w, q.w, 1e-12);
    EXPECT_NEAR(r.x, q.x, 1e-12);
    EXPECT_NEAR(r.y, q.y, 1e-12);
    EXPECT_NEAR(r.z, q.z, 1e-12);
}

TEST(Quaternion, MultiplicationAssociativity)
{
    Quaternion a = Quaternion::from_euler(0.1, 0.2, 0.3);
    Quaternion b = Quaternion::from_euler(0.4, 0.5, 0.6);
    Quaternion c = Quaternion::from_euler(0.7, 0.8, 0.9);
    Quaternion ab_c = (a * b) * c;
    Quaternion a_bc = a * (b * c);
    EXPECT_NEAR(ab_c.w, a_bc.w, 1e-12);
    EXPECT_NEAR(ab_c.x, a_bc.x, 1e-12);
    EXPECT_NEAR(ab_c.y, a_bc.y, 1e-12);
    EXPECT_NEAR(ab_c.z, a_bc.z, 1e-12);
}

TEST(Quaternion, Conjugate)
{
    Quaternion q(0.5, 0.5, 0.5, 0.5);
    Quaternion c = q.conjugate();
    EXPECT_DOUBLE_EQ(c.w, 0.5);
    EXPECT_DOUBLE_EQ(c.x, -0.5);
    EXPECT_DOUBLE_EQ(c.y, -0.5);
    EXPECT_DOUBLE_EQ(c.z, -0.5);
}

TEST(Quaternion, ConjugateTimesOriginalIsIdentity)
{
    Quaternion q = Quaternion::from_euler(0.3, 0.5, 0.7).normalized();
    Quaternion r = q * q.conjugate();
    EXPECT_NEAR(r.w, 1.0, 1e-12);
    EXPECT_NEAR(r.x, 0.0, 1e-12);
    EXPECT_NEAR(r.y, 0.0, 1e-12);
    EXPECT_NEAR(r.z, 0.0, 1e-12);
}

TEST(Quaternion, Normalization)
{
    Quaternion q(2.0, 0.0, 0.0, 0.0);
    Quaternion n = q.normalized();
    EXPECT_NEAR(n.norm(), 1.0, 1e-12);
    EXPECT_NEAR(n.w, 1.0, 1e-12);
}

TEST(Quaternion, NormalizationOfZero)
{
    Quaternion q(0.0, 0.0, 0.0, 0.0);
    Quaternion n = q.normalized();
    EXPECT_DOUBLE_EQ(n.w, 1.0);
    EXPECT_DOUBLE_EQ(n.x, 0.0);
}

TEST(Quaternion, RotateIdentityPreservesVector)
{
    Quaternion q = Quaternion::identity();
    Vec3 v(1.0, 2.0, 3.0);
    Vec3 r = q.rotate(v);
    EXPECT_NEAR(r.x, 1.0, 1e-12);
    EXPECT_NEAR(r.y, 2.0, 1e-12);
    EXPECT_NEAR(r.z, 3.0, 1e-12);
}

TEST(Quaternion, Rotate90DegreesAroundZ)
{
    // Yaw 90 degrees: x-axis maps to y-axis
    Quaternion q = Quaternion::from_euler(0.0, 0.0, kPi / 2.0);
    Vec3 v(1.0, 0.0, 0.0);
    Vec3 r = q.rotate(v);
    EXPECT_NEAR(r.x, 0.0, 1e-6);
    EXPECT_NEAR(r.y, 1.0, 1e-6);
    EXPECT_NEAR(r.z, 0.0, 1e-6);
}

TEST(Quaternion, InverseRotateUndoesRotate)
{
    Quaternion q = Quaternion::from_euler(0.3, 0.5, 0.7);
    Vec3 v(1.0, 2.0, 3.0);
    Vec3 rotated = q.rotate(v);
    Vec3 back = q.inverse_rotate(rotated);
    EXPECT_NEAR(back.x, v.x, 1e-10);
    EXPECT_NEAR(back.y, v.y, 1e-10);
    EXPECT_NEAR(back.z, v.z, 1e-10);
}

TEST(Quaternion, RotatePreservesMagnitude)
{
    Quaternion q = Quaternion::from_euler(1.0, 0.5, 2.0);
    Vec3 v(3.0, 4.0, 0.0);
    Vec3 r = q.rotate(v);
    EXPECT_NEAR(r.magnitude(), v.magnitude(), 1e-10);
}

TEST(Quaternion, Derivative)
{
    Quaternion q = Quaternion::identity();
    Vec3 omega(0.0, 0.0, 1.0); // pure yaw rotation
    Quaternion dq = q.derivative(omega);
    // dq/dt = 0.5 * q * omega_q = 0.5 * (0, 0, 0, 1) for identity * (0,0,0,1)
    EXPECT_NEAR(dq.w, 0.0, 1e-12);
    EXPECT_NEAR(dq.x, 0.0, 1e-12);
    EXPECT_NEAR(dq.y, 0.0, 1e-12);
    EXPECT_NEAR(dq.z, 0.5, 1e-12);
}

TEST(Quaternion, EulerRoundtrip)
{
    double roll = 0.3, pitch = 0.5, yaw = 0.7;
    Quaternion q = Quaternion::from_euler(roll, pitch, yaw);
    Vec3 euler = q.to_euler();
    EXPECT_NEAR(euler.x, roll, 1e-10);
    EXPECT_NEAR(euler.y, pitch, 1e-10);
    EXPECT_NEAR(euler.z, yaw, 1e-10);
}

TEST(Quaternion, EulerRoundtripZero)
{
    Quaternion q = Quaternion::from_euler(0.0, 0.0, 0.0);
    Vec3 euler = q.to_euler();
    EXPECT_NEAR(euler.x, 0.0, 1e-12);
    EXPECT_NEAR(euler.y, 0.0, 1e-12);
    EXPECT_NEAR(euler.z, 0.0, 1e-12);
}

// ── Known Euler Angle Rotations ─────────────────

TEST(EulerAngles, Pitch90Degrees)
{
    // Pitch up 90 degrees: body x-axis should point up in NED (negative z)
    Quaternion q = Quaternion::from_euler(0.0, kPi / 2.0, 0.0);
    Vec3 body_x(1.0, 0.0, 0.0);
    Vec3 ned = q.rotate(body_x);
    EXPECT_NEAR(ned.x, 0.0, 1e-6);
    EXPECT_NEAR(ned.y, 0.0, 1e-6);
    EXPECT_NEAR(ned.z, -1.0, 1e-6);
}

TEST(EulerAngles, Roll90Degrees)
{
    // Roll 90 degrees right: body y-axis should point down in NED (+z)
    Quaternion q = Quaternion::from_euler(kPi / 2.0, 0.0, 0.0);
    Vec3 body_y(0.0, 1.0, 0.0);
    Vec3 ned = q.rotate(body_y);
    EXPECT_NEAR(ned.x, 0.0, 1e-6);
    EXPECT_NEAR(ned.y, 0.0, 1e-6);
    EXPECT_NEAR(ned.z, 1.0, 1e-6);
}

TEST(EulerAngles, Yaw180Degrees)
{
    // Yaw 180 degrees: body x points south (negative north)
    Quaternion q = Quaternion::from_euler(0.0, 0.0, kPi);
    Vec3 body_x(1.0, 0.0, 0.0);
    Vec3 ned = q.rotate(body_x);
    EXPECT_NEAR(ned.x, -1.0, 1e-6);
    EXPECT_NEAR(ned.y, 0.0, 1e-6);
    EXPECT_NEAR(ned.z, 0.0, 1e-6);
}

// ── Utility Functions ───────────────────────────

TEST(Utility, ClampWithinRange)
{
    EXPECT_DOUBLE_EQ(clamp(0.5, 0.0, 1.0), 0.5);
}

TEST(Utility, ClampBelowMin)
{
    EXPECT_DOUBLE_EQ(clamp(-1.0, 0.0, 1.0), 0.0);
}

TEST(Utility, ClampAboveMax)
{
    EXPECT_DOUBLE_EQ(clamp(2.0, 0.0, 1.0), 1.0);
}

TEST(Utility, ClampAtBoundaries)
{
    EXPECT_DOUBLE_EQ(clamp(0.0, 0.0, 1.0), 0.0);
    EXPECT_DOUBLE_EQ(clamp(1.0, 0.0, 1.0), 1.0);
}

TEST(Utility, LerpAtZero)
{
    EXPECT_DOUBLE_EQ(lerp(10.0, 20.0, 0.0), 10.0);
}

TEST(Utility, LerpAtOne)
{
    EXPECT_DOUBLE_EQ(lerp(10.0, 20.0, 1.0), 20.0);
}

TEST(Utility, LerpMidpoint)
{
    EXPECT_DOUBLE_EQ(lerp(10.0, 20.0, 0.5), 15.0);
}

TEST(Utility, LerpExtrapolation)
{
    EXPECT_DOUBLE_EQ(lerp(0.0, 10.0, 2.0), 20.0);
}
