# Flight Dynamics Theory

This document covers the physics and mathematics behind luft's flight dynamics simulation. It maps directly to the implementation in `src/core/flight_dynamics.cpp`, `src/core/aerodynamics.cpp`, `src/core/atmosphere.cpp`, and `src/core/engine_model.cpp`. All equations are written in text form.

---

## 1. Reference Frames

Flight dynamics requires at least two coordinate frames: one fixed to the earth and one fixed to the aircraft. Luft uses NED (North-East-Down) as the world frame and a conventional body frame attached to the airframe.

### NED (North-East-Down) -- World Frame

| Axis | Direction | Sign convention |
|------|-----------|-----------------|
| x | North | positive northward |
| y | East | positive eastward |
| z | Down | positive toward earth's center |

Position is expressed in NED meters relative to an arbitrary origin. Because z points down, altitude above mean sea level is the negation of the z component:

    altitude_msl = -position.z

An aircraft at 1000 m altitude has `position.z = -1000`. This convention is universal in aerospace engineering because it produces a right-handed coordinate system where the x-y plane is the local tangent plane and z aligns with gravity.

### Body Frame

| Axis | Direction | Sign convention |
|------|-----------|-----------------|
| x | Forward | positive out the nose |
| y | Right | positive out the right wing |
| z | Down | positive through the belly |

The body frame is rigidly attached to the aircraft and moves with it. All velocities, angular velocities, forces, and moments are expressed in this frame during computation. The axes align with the aircraft's principal geometric directions, making physical interpretation straightforward: a positive x-velocity means the aircraft is moving forward, a positive y-velocity means it is slipping to the right, and a positive z-velocity means it is descending relative to its own orientation.

### How the Two Frames Relate

The orientation quaternion `q` transforms vectors between frames:

    v_NED  = q.rotate(v_body)            body to world
    v_body = q.inverse_rotate(v_NED)     world to body

When the aircraft is in its identity orientation (quaternion = [1, 0, 0, 0]), the body frame is aligned with NED: nose points north, right wing points east, belly points down. Any deviation from this -- pitching up, banking, turning -- is captured by the quaternion.

The relationship is equivalent to the Direction Cosine Matrix (DCM), but stored as four numbers instead of nine. The quaternion encodes the same rotation without redundancy or singularity.

---

## 2. Aircraft State Vector

The complete aircraft state integrated by the simulation is defined in `AircraftState` (`aircraft_state.h`):

| Variable | Symbol | Frame | Units | Description |
|----------|--------|-------|-------|-------------|
| Position | p | NED | m | Location relative to origin |
| Velocity | V_b | body | m/s | Linear velocity of CG |
| Orientation | q | body-to-NED quaternion | -- | Attitude representation |
| Angular velocity | omega | body | rad/s | Rotation rates [p, q, r] |
| Thrust | T_current | scalar | N | Current engine thrust |
| Fuel mass | m_fuel | scalar | kg | Remaining fuel |

The position is in the world frame because it describes where the aircraft is in space. The velocity and angular velocity are in the body frame because the equations of motion are naturally formulated there -- the aerodynamic forces depend on the airflow relative to the aircraft, not relative to the ground.

The quaternion stores the rotation from body to NED. This is the convention used in `math_types.h`: calling `q.rotate(v)` takes a body-frame vector and returns its NED representation.

### Derived Quantities

These are recomputed each time step from the primary state variables:

| Variable | Symbol | Definition |
|----------|--------|------------|
| Airspeed | V | magnitude of (V_b - wind_body) |
| Angle of attack | alpha | atan2(w_air, u_air) |
| Sideslip angle | beta | asin(v_air / V) |
| Dynamic pressure | q_bar | 0.5 * rho * V^2 |
| Altitude MSL | h | -p_z |

Here (u_air, v_air, w_air) are the components of the air-relative velocity in the body frame. The air-relative velocity is the aircraft's body velocity minus the wind velocity (also expressed in the body frame). These are only computed when V > 1.0 m/s to avoid division by zero at very low speeds.

The angle of attack (alpha) is the angle between the aircraft's x-axis and the projection of the airflow onto the x-z plane. Sideslip (beta) is the angle between the airflow and the x-z plane. Together they fully describe the direction of the airflow relative to the aircraft.

---

## 3. Forces

Three categories of forces act on the aircraft. All are resolved into the body frame before being summed.

### Gravity

Gravity is constant in the NED frame -- it always pulls straight down regardless of the aircraft's attitude:

    F_gravity_NED = [0, 0, m * g]

where g = 9.80665 m/s^2 and m = empty_mass + fuel_mass.

To use this in the body-frame equations of motion, gravity is rotated from NED into the body frame:

    F_gravity_body = q.inverse_rotate(F_gravity_NED)

When the aircraft is level, this gives [0, 0, m*g] in body coordinates (gravity pulls through the belly). When the aircraft pitches up 30 degrees, part of gravity acts along the negative body x-axis (opposing forward motion) and part still acts along body z. This is how the simulation correctly models the "gravity component along the flight path" that makes climbing aircraft decelerate and diving aircraft accelerate -- without any special-case logic.

### Aerodynamic Forces

Aerodynamic forces arise from the interaction between the airframe and the air mass. They are naturally defined in the stability axis system, which is aligned with the airflow:

- Lift (L): perpendicular to the freestream airflow, in the aircraft's plane of symmetry. Lift opposes the component of weight that acts perpendicular to the flight path.
- Drag (D): parallel to the freestream airflow, opposing the direction of motion through the air. Drag always decelerates the aircraft relative to the air mass.
- Side force (Y): perpendicular to the plane of symmetry, caused by sideslip or rudder deflection.

Lift and drag are computed in the stability axis, then rotated into the body frame by the angle of attack (alpha):

    L = q_bar * S * CL
    D = q_bar * S * CD
    Y = q_bar * S * CY

    F_aero_x = -D * cos(alpha) + L * sin(alpha)
    F_aero_y = Y
    F_aero_z = -D * sin(alpha) - L * cos(alpha)

The negative signs appear because drag opposes motion (negative x in the stability axis) and lift acts "upward" relative to the airflow (negative z in the stability axis). The alpha rotation projects these correctly into the body x-z plane. At zero angle of attack, F_aero_x = -D (pure rearward drag) and F_aero_z = -L (pure upward lift in body coordinates, where z is down). At nonzero alpha, some lift contributes to the body x-direction and vice versa.

S is the wing reference area (16.2 m^2 for the default Cessna 172 parameters).

### Thrust

Thrust acts along the body x-axis (forward through the nose). The simulation does not model thrust line offset or thrust vectoring:

    F_thrust = [T_current, 0, 0]

### Total Force

All forces are summed in the body frame:

    F_total = F_aero + F_thrust + F_gravity_body

This is the net force used in the translational equation of motion.

---

## 4. Moments

Aerodynamic moments are torques about the aircraft's center of gravity, expressed in the body frame. Each moment corresponds to a rotation about one body axis.

### Roll Moment (L_moment, about body x-axis)

    L_moment = q_bar * S * b * Cl

Roll moment causes the aircraft to rotate about its longitudinal axis (one wing goes up, the other goes down). It is generated by:

- Asymmetric lift distribution from aileron deflection (primary roll control)
- Sideslip interacting with wing dihedral and sweep (the "dihedral effect," Clb)
- Yaw rate coupling (Clr) -- when yawing, the advancing wing sees higher airspeed and generates more lift
- Roll rate damping (Clp) -- a rolling wing generates asymmetric angles of attack that oppose the roll
- Rudder deflection (Cldr) -- the vertical tail is above the CG, so rudder force creates a small roll

The reference length for roll moment is the wing span (b).

### Pitch Moment (M_moment, about body y-axis)

    M_moment = q_bar * S * c * Cm

Pitch moment causes the nose to rotate up or down. It is generated by:

- Angle of attack shift from the aerodynamic center (Cma) -- this is the primary source of pitch stability
- Elevator deflection (Cmde) -- the horizontal tail generates a pitching force at a long moment arm
- Pitch rate damping (Cmq) -- the tail sees additional angle of attack when the aircraft pitches, opposing the motion
- Flap deflection (Cmdf) -- flaps shift the wing's lift distribution aft, typically producing a nose-down moment
- A zero-alpha offset (Cm0) -- accounts for wing camber and incidence angle

The reference length for pitch moment is the mean aerodynamic chord (c).

### Yaw Moment (N_moment, about body z-axis)

    N_moment = q_bar * S * b * Cn

Yaw moment causes the nose to swing left or right. It is generated by:

- Sideslip acting on the vertical tail (Cnb) -- the "weathercock" effect that provides directional stability
- Rudder deflection (Cndr) -- primary yaw control
- Yaw rate damping (Cnr) -- the vertical tail opposes established yaw rates
- Roll rate coupling (Cnp) -- rolling creates asymmetric drag that yaws the aircraft
- Aileron deflection (Cnda) -- "adverse yaw," the down-going aileron creates more drag than the up-going one

The reference length for yaw moment is the wing span (b), same as for roll.

---

## 5. Aerodynamic Coefficients and Stability Derivatives

The aerodynamic model uses a linearized stability derivative approach. Each non-dimensional coefficient is expressed as a linear sum of contributions from the flight condition, angular rates, and control deflections. This is valid for small perturbations around a reference condition and is the standard approach for general aviation flight simulators.

### Angle of Attack and Sideslip

The two aerodynamic angles that define the direction of airflow relative to the aircraft:

    alpha = atan2(w_air, u_air)
    beta  = asin(v_air / V)

Alpha (angle of attack) is the angle between the body x-axis and the projection of the airflow onto the body x-z plane. It is the primary driver of lift. At small alpha, lift increases linearly; beyond the critical angle (typically 15-20 degrees for general aviation), the wing stalls and lift drops.

Beta (sideslip angle) is the angle between the total airflow vector and the body x-z plane. Zero sideslip means the air comes straight over the nose; nonzero sideslip means the aircraft is "flying sideways" to some degree. Sideslip drives lateral forces and yaw/roll coupling.

### Non-Dimensional Angular Rates

Raw angular rates (rad/s) must be non-dimensionalized before use in coefficient equations, because the aerodynamic effect of a given rotation rate depends on the aircraft's size and speed:

    p_hat = p * b / (2 * V)     roll rate, normalized by span
    q_hat = q * c / (2 * V)     pitch rate, normalized by chord
    r_hat = r * b / (2 * V)     yaw rate, normalized by span

The factor of 2V in the denominator is conventional. Physically, p_hat represents the ratio of the wingtip velocity (due to roll) to the freestream velocity. A p_hat of 0.1 means the wingtip is moving at 10% of the freestream speed due to the roll alone. Similarly, q_hat represents how much additional angle of attack the tail sees due to pitch rate, relative to the freestream.

Roll and yaw rates use the span (b) because the relevant aerodynamic surfaces (wingtips, vertical tail) are at span-wise distances from the CG. Pitch rate uses the chord (c) because the relevant surface (horizontal tail) is at a chord-wise distance.

### Longitudinal Coefficients

These govern motion in the aircraft's plane of symmetry (pitch and forward/vertical motion):

    CL = CL0 + CLa * alpha + CLde * delta_e + CLq * q_hat + CLdf * delta_f
    CD = CD0 + CDi_k * CL^2
    Cm = Cm0 + Cma * alpha + Cmde * delta_e + Cmq * q_hat + Cmdf * delta_f

| Symbol | Meaning | Default | Description |
|--------|---------|---------|-------------|
| CL0 | Lift at zero AoA | 0.307 | Comes from wing camber and incidence |
| CLa | Lift curve slope (per rad) | 4.73 | How fast lift grows with AoA; theoretical max is 2*pi per rad for a thin airfoil, less for finite wings |
| CLde | Lift due to elevator (per rad) | 0.43 | Elevator changes tail lift, which adds to total |
| CLq | Lift due to pitch rate | 3.9 | Pitching up increases tail AoA, adding lift |
| CLdf | Lift due to flaps (per rad) | 0.8 | Flaps increase wing camber, boosting CL |
| CD0 | Parasite drag | 0.027 | Skin friction + form drag at zero lift |
| CDi_k | Induced drag factor | 0.054 | Equals 1/(pi * e * AR) where e is Oswald efficiency and AR is aspect ratio |
| Cm0 | Pitching moment at zero AoA | 0.04 | Inherent nose-up or nose-down tendency |
| Cma | Pitch stiffness (per rad) | -0.613 | Negative means stable: increased AoA produces nose-down moment that corrects |
| Cmde | Pitch control power (per rad) | -1.122 | How much moment per radian of elevator; negative means aft stick produces nose-up |
| Cmq | Pitch damping | -12.4 | Negative means pitch rate is resisted -- oscillations damp out |
| Cmdf | Pitching moment due to flaps | -0.1 | Flaps typically produce a nose-down moment |

The drag model uses a parabolic polar: CD = CD0 + k * CL^2. The first term is parasite drag (present even at zero lift), and the second is induced drag (the unavoidable cost of generating lift with a finite wing). The factor k = 1/(pi * e * AR) where e is the Oswald span efficiency factor (~0.8) and AR is the wing aspect ratio.

### Lateral-Directional Coefficients

These govern motion out of the symmetry plane (roll, yaw, and sideslip):

    CY = Cyb * beta + Cydr * delta_r
    Cl = Clb * beta + Clp * p_hat + Clr * r_hat + Clda * delta_a + Cldr * delta_r
    Cn = Cnb * beta + Cnp * p_hat + Cnr * r_hat + Cnda * delta_a + Cndr * delta_r

| Symbol | Meaning | Default | Description |
|--------|---------|---------|-------------|
| Cyb | Side force due to sideslip (per rad) | -0.31 | Fuselage and vertical tail resist sideslip |
| Cydr | Side force due to rudder (per rad) | 0.187 | Rudder deflection generates side force on the tail |
| Clb | Dihedral effect (per rad) | -0.089 | Sideslip produces a roll moment toward wings-level; negative is stabilizing |
| Clp | Roll damping | -0.47 | The rolling wing sees asymmetric AoA that opposes roll; always negative |
| Clr | Roll due to yaw rate | 0.096 | Yawing makes the advancing wing faster, generating more lift |
| Clda | Aileron roll power (per rad) | -0.178 | How much roll moment per radian of aileron deflection |
| Cldr | Roll due to rudder (per rad) | 0.0147 | Rudder force acts above CG, producing a small roll |
| Cnb | Weathercock stability (per rad) | 0.065 | Sideslip produces yaw toward the wind; positive is stabilizing |
| Cnp | Yaw due to roll rate | -0.03 | Rolling creates asymmetric drag distribution |
| Cnr | Yaw damping | -0.099 | Vertical tail resists yaw rate; always negative |
| Cnda | Adverse yaw (per rad) | 0.02 | Ailerons create unequal drag, yawing opposite to intended turn |
| Cndr | Rudder yaw power (per rad) | -0.074 | How much yaw moment per radian of rudder deflection |

### How Control Surfaces Map to Coefficients

Each control surface has a primary effect and secondary cross-coupling effects:

- Elevator (delta_e): primarily affects Cm (pitch moment) and secondarily CL (total lift changes slightly with tail incidence). The elevator is the main pitch control.
- Aileron (delta_a): primarily affects Cl (roll moment). The secondary effect is Cn via adverse yaw -- the down-going aileron produces more drag, yawing the nose opposite to the turn direction. Pilots coordinate with rudder to cancel this.
- Rudder (delta_r): primarily affects Cn (yaw moment) and secondarily CY (side force) and Cl (roll, because the tail is above the CG).
- Flaps (delta_f): primarily affect CL (increased lift at a given AoA) and secondarily Cm (pitching moment shift) and CD (increased drag from the flap deflection, captured indirectly through the CL^2 term in the drag polar).

---

## 6. Control Surfaces

Control inputs are normalized and then mapped to physical deflections in radians:

    delta_e = elevator * max_elevator     (max 28 deg = 0.489 rad)
    delta_a = aileron  * max_aileron      (max 20 deg = 0.349 rad)
    delta_r = rudder   * max_rudder       (max 16 deg = 0.279 rad)
    delta_f = flaps    * max_flap         (max 40 deg = 0.698 rad)

| Surface | Input range | Primary axis | What it does |
|---------|-------------|-------------|--------------|
| Elevator | [-1, 1] | Pitch (body y) | Deflects the horizontal tail to create a pitching moment. Positive input = nose up. Controlled by fore/aft stick. |
| Aileron | [-1, 1] | Roll (body x) | Deflects differentially on left and right wings. One goes up (less lift), the other goes down (more lift), creating a roll moment. Positive input = roll right. Controlled by left/right stick. |
| Rudder | [-1, 1] | Yaw (body z) | Deflects the vertical tail to create a yawing moment. Positive input = yaw right. Controlled by pedals. |
| Flaps | [0, 1] | Lift augmentation | Extends from the trailing edge of the inner wing. Increases wing camber and area, producing more lift at a given airspeed. Also increases drag substantially. Used during slow flight, approach, and landing. Not a control surface in the rotational sense -- flaps are set and left. |
| Throttle | [0, 1] | Thrust | Controls engine power. Not an aerodynamic surface, but a critical "control." |
| Trim | [-1, 1] | Pitch offset | An offset added to the elevator to relieve sustained stick force. Allows hands-off flight at a desired speed. |

---

## 7. Six-Degree-of-Freedom Equations of Motion

The 6-DOF equations describe the complete motion of a rigid body with three translational and three rotational freedoms. They are formulated in the body frame, which rotates with the aircraft. This is the standard aerospace convention because aerodynamic forces and moments are most naturally expressed relative to the aircraft.

### Translational Equation

    V_body_dot = F_total / m - omega x V_body

This is Newton's second law (F = m * a) written in a rotating reference frame. The term `omega x V_body` is the Coriolis/centripetal correction that accounts for the fact that the body frame is not inertial -- it rotates with the aircraft. Without this term, a level turn at constant speed would incorrectly show zero acceleration in the body frame, when in fact centripetal acceleration is needed to maintain the turn.

Expanding in components (where [u, v, w] = V_body and [p, q, r] = omega):

    u_dot = Fx/m + r*v - q*w
    v_dot = Fy/m + p*w - r*u
    w_dot = Fz/m + q*u - p*v

For example, in a steady coordinated turn at constant altitude and speed, u_dot = v_dot = w_dot = 0, and the force and Coriolis terms balance exactly.

### Position Update

Position is in the NED frame, so the body velocity must be rotated:

    p_dot = q.rotate(V_body)

This converts the body velocity to NED coordinates. The quaternion `q` encodes the current aircraft attitude, so this rotation correctly accounts for pitch, roll, and yaw when computing the ground track.

### Rotational Equation (Euler's Equation for Rigid Bodies)

    omega_dot = I_inv * (M_total - omega x (I * omega))

This is the rotational analog of Newton's second law. The term `omega x (I * omega)` is the gyroscopic coupling that arises because the angular momentum vector is not generally aligned with the angular velocity vector (the inertia tensor is not a scalar). This coupling is responsible for effects like:

- Pitch-roll coupling in high-alpha flight
- Gyroscopic precession from the spinning propeller (not modeled separately here, but the framework supports it)
- The tendency for roll oscillations to couple into yaw and vice versa

The inertia tensor for a typical aircraft (symmetric about the x-z plane) is:

        [ Ixx    0   -Ixz ]
    I = [  0    Iyy    0  ]
        [-Ixz    0    Izz ]

The off-diagonal term Ixz captures the mass distribution asymmetry between the front/rear and top/bottom of the aircraft. For the Cessna 172 parameters, Ixz = 0, which simplifies the equations but the full inversion is implemented.

Computing I * omega:

    Hx = Ixx * p - Ixz * r
    Hy = Iyy * q
    Hz = Izz * r - Ixz * p

The gyroscopic term is omega x H:

    gyro = omega x [Hx, Hy, Hz]

The right-hand side becomes:

    rhs = M_total - gyro

The inertia matrix is inverted analytically:

    det = Ixx * Izz - Ixz^2

    omega_dot_x = (Izz * rhs_x + Ixz * rhs_z) / det
    omega_dot_y = rhs_y / Iyy
    omega_dot_z = (Ixz * rhs_x + Ixx * rhs_z) / det

The y-axis (pitch) decouples because Ixy = Iyz = 0 for a symmetric aircraft. Roll and yaw couple through Ixz.

### Complete State Derivative Vector

Putting it all together, the function `compute_derivatives` returns:

    p_dot         = q.rotate(V_body)                             position rate in NED
    V_body_dot    = F_total / m  -  omega x V_body               body acceleration
    q_dot         = 0.5 * q * [0, omega]                         quaternion rate
    omega_dot     = I_inv * (M_total - omega x (I * omega))      angular acceleration

These four derivatives are evaluated at each RK4 sub-step.

---

## 8. Quaternions vs. Euler Angles

### The Gimbal Lock Problem

Euler angles represent orientation as three sequential rotations: yaw (psi, about z), pitch (theta, about y), then roll (phi, about x). This decomposition has a fundamental singularity at pitch = +/- 90 degrees, known as gimbal lock.

At pitch = 90 degrees (nose straight up), the yaw axis and the roll axis become parallel. Changes in yaw and changes in roll produce the same physical rotation, so the system loses one degree of freedom. Mathematically, the kinematic equations that convert angular velocity to Euler angle rates contain a 1/cos(theta) term:

    phi_dot   = p + (q * sin(phi) + r * cos(phi)) * tan(theta)
    theta_dot = q * cos(phi) - r * sin(phi)
    psi_dot   = (q * sin(phi) + r * cos(phi)) / cos(theta)

As theta approaches 90 degrees, tan(theta) and 1/cos(theta) approach infinity. The integration blows up, producing nonsensical results or numerical overflow. This is not a software bug -- it is an inherent limitation of the three-parameter Euler angle representation.

For a flight simulator, gimbal lock is a practical problem: aerobatic maneuvers, spins, and even steep climbs approach the singularity. Any general-purpose 6-DOF simulator must avoid Euler angles for state integration.

### Why Quaternions

A quaternion q = [w, x, y, z] is a four-parameter representation of orientation. It has no singularities -- every orientation maps to exactly two quaternions (q and -q, which represent the same rotation). The quaternion derivative is:

    dq/dt = 0.5 * q * omega_quat

where omega_quat = [0, p, q, r] is the angular velocity expressed as a pure quaternion (zero scalar part). This equation is smooth and well-defined for all orientations.

In the code (`math_types.h`), the `derivative` method implements this directly:

    Quaternion omega_q{0, omega.x, omega.y, omega.z};
    Quaternion dq = (*this) * omega_q;
    return {dq.w * 0.5, dq.x * 0.5, dq.y * 0.5, dq.z * 0.5};

The multiplication `(*this) * omega_q` is the standard Hamilton product, and the factor of 0.5 comes from the kinematic derivation.

### Normalization

A valid rotation quaternion must have unit magnitude: w^2 + x^2 + y^2 + z^2 = 1. Numerical integration introduces drift that violates this constraint. After each integration step, the quaternion is renormalized:

    q = q / |q|

This is cheap (one square root, four divides) and keeps the quaternion on the unit hypersphere. Without normalization, the quaternion would gradually grow or shrink, causing the rotation to scale improperly.

### Euler Angles for Display Only

Euler angles are still useful for human-readable output: "pitch 5 degrees, bank 30 degrees" is more intuitive than quaternion components. The `to_euler()` method extracts roll, pitch, and yaw from the quaternion for telemetry and HUD display. These are never fed back into the integrator.

---

## 9. RK4 Integration

### The Classical Fourth-Order Runge-Kutta Method

Luft uses the classical RK4 method to integrate the 6-DOF equations of motion:

    k1 = f(state)
    k2 = f(state + k1 * dt/2)
    k3 = f(state + k2 * dt/2)
    k4 = f(state + k3 * dt)

    state_new = state + (dt/6) * (k1 + 2*k2 + 2*k3 + k4)

The method evaluates the derivative function f() at four points within the time step: the beginning, twice at the midpoint (using two different slope estimates), and at the end. The final update is a weighted average: the midpoint evaluations get double weight.

Each evaluation of f() (i.e., `compute_derivatives`) runs the full aerodynamics model, transforms gravity, computes the Coriolis term, and inverts the inertia tensor. This means four complete aero evaluations per time step.

After integration, the quaternion is renormalized and derived quantities (airspeed, alpha, beta, dynamic pressure) are recomputed from the new state.

The engine model update is applied once before the RK4 loop rather than inside it. The first-order exponential lag of the engine is smooth enough that a single forward-Euler step is sufficient at the outer integration level.

### Comparison of Integration Methods

**Forward Euler (1st order):**

    state_new = state + dt * f(state)

One derivative evaluation per step. The local truncation error is proportional to dt^2, and the global error is proportional to dt. This means halving the time step only halves the error. Forward Euler is cheap but drifts badly -- energy is not conserved, and oscillatory modes (like the short-period pitch oscillation) either grow or damp artificially depending on the time step. For flight dynamics at 100 Hz, Euler integration produces visible "numerical dissipation" that changes the aircraft's behavior.

**RK2 / Midpoint Method (2nd order):**

    k1 = f(state)
    k2 = f(state + k1 * dt/2)
    state_new = state + dt * k2

Two derivative evaluations per step. Global error proportional to dt^2. Significantly better than Euler, but still not sufficient for the coupled nonlinear dynamics of flight. Phase errors in oscillatory modes are noticeable at typical time steps.

**RK4 (4th order):**

Four derivative evaluations per step. Global error proportional to dt^4. Halving the time step reduces the error by a factor of 16. At 100 Hz (dt = 0.01s), RK4 produces errors on the order of 10^-8 per step for smooth flight dynamics, which is far below any physically meaningful threshold.

**Why RK4 is the sweet spot:** Going from Euler to RK4 costs 4x the computation per step but gives dt^4 instead of dt error scaling. For flight dynamics, this means RK4 at 100 Hz is more accurate than Euler at 10,000 Hz, while being 25x cheaper. Higher-order methods (RK5, RK8) exist but offer diminishing returns: the aerodynamic model itself is only an approximation, so integrator error well below the modeling error is wasted precision.

### Why Fixed-Step Integration

Luft uses a fixed time step (default 0.01s = 100 Hz) rather than adaptive step-size control. This is a deliberate choice for several reasons:

**Determinism.** Given identical initial conditions and the same sequence of control inputs applied at the same tick numbers, the simulation produces bit-identical output regardless of wall-clock speed. Variable time steps depend on frame rate, which depends on system load, so two runs of the same scenario produce different results.

**Reproducibility.** Determinism enables regression testing (compare simulation traces across code changes), record-and-replay (store input sequences, replay offline), network synchronization (clients can predict state between telemetry updates), and debugging (reproduce any reported flight condition exactly).

**Stability.** Variable time steps can cause numerical instability when the step suddenly grows (e.g., after a lag spike). The integrator may overshoot, especially for stiff dynamics like rapid control inputs. Clamping to a maximum step helps but does not eliminate the issue.

**Simplicity.** Fixed-step RK4 has no step-size controller, no error estimator, no rejected steps. The code is straightforward to implement and debug.

The simulation runs a catch-up loop: it counts how many fixed steps are needed to bring simulation time up to wall-clock time, capped at a maximum per frame to prevent a "spiral of death" if the machine falls behind.

---

## 10. ISA Atmosphere Model

The `Atmosphere` class (`atmosphere.cpp`) implements the International Standard Atmosphere (ISA), a simplified model of how temperature, pressure, and density vary with altitude.

### Troposphere (0 to 11,000 m)

Temperature decreases linearly with altitude at the lapse rate L:

    T = T0 - L * h

Pressure is derived from the hydrostatic equation and the ideal gas law, assuming a linear temperature profile:

    P = P0 * (T / T0) ^ (g / (L * R))

The exponent g/(L*R) = 9.80665 / (0.0065 * 287.058) = 5.256. This is not an arbitrary fit -- it is the exact solution to the differential equation dP/dh = -rho*g combined with the ideal gas law P = rho*R*T and the linear temperature assumption.

Density follows from the ideal gas law:

    rho = P / (R * T)

### Lower Stratosphere (11,000 to 20,000 m)

Above the tropopause, temperature is constant at T_tropopause = 288.15 - 0.0065 * 11000 = 216.65 K (-56.5 C). The pressure equation simplifies to:

    P = P_tropopause * exp(-g * (h - 11000) / (R * T_tropopause))

This is the isothermal atmosphere formula. Density again follows from rho = P / (R * T). Altitudes above 20,000 m are clamped to the 20,000 m value; the simulation is intended for general aviation altitudes where this model is accurate.

### Constants

| Symbol | Value | Units | Description |
|--------|-------|-------|-------------|
| T0 | 288.15 | K | Sea level temperature (15 C) |
| P0 | 101325 | Pa | Sea level pressure |
| rho0 | 1.225 | kg/m^3 | Sea level density |
| L | 0.0065 | K/m | Temperature lapse rate |
| g | 9.80665 | m/s^2 | Standard gravity |
| R | 287.058 | J/(kg*K) | Specific gas constant for dry air |
| gamma | 1.4 | -- | Ratio of specific heats (cp/cv) for air |

### Speed of Sound

The speed of sound depends only on temperature (for an ideal gas):

    a = sqrt(gamma * R * T)

At sea level: a = sqrt(1.4 * 287.058 * 288.15) = 340.3 m/s. This is used to compute Mach number for displays and could be used for compressibility corrections in a more detailed aerodynamic model.

---

## 11. Engine Model

The `EngineModel` class (`engine_model.cpp`) represents a simple piston engine with a first-order thrust response lag.

### Thrust Response: First-Order Lag

Real engines cannot change thrust instantaneously. Piston engines have intake manifold dynamics, turbocharger lag, and propeller inertia. The simulation models this as a first-order exponential lag:

    thrust_target = max_thrust * (idle_thrust_frac + (1 - idle_thrust_frac) * throttle) * density_ratio
    thrust_dot = (thrust_target - thrust_current) / tau
    thrust_current = thrust_current + thrust_dot * dt

where tau is the spool time constant (default 1.0 second). This means the thrust exponentially approaches the target value, reaching about 63% of the way in one time constant and 95% in three time constants. Rapid throttle changes produce a smooth thrust response rather than an instantaneous jump.

The idle_thrust_frac (default 0.03) ensures that a small amount of thrust exists at zero throttle, representing engine idle.

### Density Ratio

Engine output depends on air density because the engine breathes ambient air. A naturally aspirated piston engine produces power roughly proportional to the mass of air it ingests per cycle. At altitude, the air is thinner:

    density_ratio = rho / rho_sea_level

At 3000 m, density is about 0.74 of sea level. This means max available thrust at 3000 m is only 74% of the sea-level value. The simulation captures this by scaling the target thrust by the density ratio:

    thrust_target = max_thrust * throttle_fraction * density_ratio

This is a first approximation. Real engines have more complex altitude behavior (turbocharging, mixture control, manifold pressure limits), but the density ratio is the dominant effect.

### Fuel Consumption

Fuel flow is proportional to thrust via the specific fuel consumption (SFC):

    fuel_flow = sfc * thrust_current       (kg/s)
    fuel_mass = fuel_mass - fuel_flow * dt

The SFC (default 0.00008 kg/(N*s)) represents how many kilograms of fuel are burned per newton of thrust per second. When fuel is exhausted (fuel_mass <= 0), thrust drops to zero immediately.

### Default Engine Parameters

| Parameter | Value | Description |
|-----------|-------|-------------|
| max_thrust | 2400 N | Approximate equivalent thrust for a Lycoming O-360 |
| idle_thrust_frac | 0.03 | 3% of max thrust at idle |
| engine_spool_tau | 1.0 s | Time constant for thrust response |
| sfc | 0.00008 kg/(N*s) | Specific fuel consumption |

---

## 12. Stability and Trim

### What is Trim?

Trimmed flight is the condition where all net forces and moments on the aircraft are zero (or, more precisely, produce the desired steady-state motion). In steady level flight at constant speed:

- Net vertical force = 0: lift equals weight (L = m * g)
- Net horizontal force = 0: thrust equals drag (T = D)
- Net pitching moment = 0: the elevator is set so Cm = 0 at the required alpha
- Net roll and yaw moments = 0: wings level, no sideslip

Finding the trim condition means solving for the combination of alpha (which sets CL and CD), thrust (which sets throttle), and elevator deflection (which sets Cm) that satisfies all three conditions simultaneously. Luft does not have an automatic trim solver; the pilot (or an external controller) must adjust throttle and the elevator trim input to achieve equilibrium.

### Static Stability

Static stability asks: if the aircraft is disturbed from trim, do the resulting forces and moments push it back toward the trim condition?

**Longitudinal (pitch) stability** requires Cma < 0. If the aircraft encounters a gust that increases alpha, the resulting pitching moment must be nose-down (negative Cm) to reduce alpha and restore equilibrium. The default value Cma = -0.613 means that each radian increase in alpha produces a -0.613 nose-down pitching moment coefficient. This is the most important single stability criterion. Physically, it requires that the aircraft's center of gravity is ahead of the aerodynamic center (the point where pitching moment does not change with alpha). The horizontal tail, located behind the CG, provides the restoring moment.

**Directional (yaw) stability** requires Cnb > 0. If the aircraft develops sideslip (beta > 0, air coming from the right), the resulting yaw moment must turn the nose to the right (positive Cn, toward the wind) to reduce beta. This is the "weathercock" effect, provided primarily by the vertical tail. The default value Cnb = 0.065 satisfies this.

**Lateral (roll) stability** requires Clb < 0. If the aircraft develops sideslip to the right (beta > 0), the resulting roll moment must roll the aircraft to the left (negative Cl, away from the sideslip) to level the wings and reduce the sideslip. This "dihedral effect" comes from wing dihedral angle, wing sweep, and high-wing configuration. The default value Clb = -0.089 satisfies this.

The default Cessna 172 parameters satisfy all three stability conditions.

### Dynamic Stability

Static stability is necessary but not sufficient. An aircraft can be statically stable but dynamically unstable if disturbances cause oscillations that grow over time.

Dynamic stability depends on the damping derivatives:

- Cmq (pitch damping): must be negative. It opposes pitch rate, so oscillations in the short-period mode (the fast pitch oscillation) damp out. Default: -12.4.
- Clp (roll damping): must be negative. It opposes roll rate directly. A rolling wing generates asymmetric angles of attack that resist the roll. Default: -0.47.
- Cnr (yaw damping): must be negative. The vertical tail provides a restoring force proportional to yaw rate, damping the Dutch roll mode. Default: -0.099.

All three are negative in the default parameter set, ensuring that the short-period, roll subsidence, and Dutch roll modes are all damped.

### Why Cma < 0 Means Stable: A Closer Look

Consider an aircraft trimmed at some alpha_trim with Cm = 0. Now a gust increases alpha by a small amount d_alpha:

    Cm_new = Cm_old + Cma * d_alpha = 0 + Cma * d_alpha

If Cma < 0, then Cm_new < 0, meaning a nose-down moment. This pitches the aircraft nose-down, reducing alpha back toward alpha_trim. The disturbance is corrected -- the aircraft is stable.

If Cma > 0, the moment would be nose-up, increasing alpha further. The aircraft diverges from trim -- unstable.

If Cma = 0, there is no restoring moment. The aircraft stays at whatever alpha the gust pushed it to -- neutrally stable.

The magnitude of Cma determines how aggressively the aircraft corrects. A very negative Cma makes the aircraft stiff in pitch (hard to disturb, but also harder to maneuver). A slightly negative Cma gives a more maneuverable but less stable aircraft. Fighter aircraft may have Cma close to zero or even slightly positive, requiring active stability augmentation (fly-by-wire).
