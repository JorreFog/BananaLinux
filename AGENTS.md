```markdown
# BananaLinux Agent Rules & Constraints

1. **Hardware Scope (Strict):**
   - All code, scripts, UI components, and driver hooks must target **ONLY** the Anbernic RG DS and RG DS Plus handhelds (Rockchip RK3566/RK3568, dual-screen layout, i2c touch, DRM/KMS).
   - Never introduce multi-device abstractions, generic Linux desktop packages, or unsupported hardware drivers.

2. **Architecture & Minimalism:**
   - Adhere strictly to the read-only squashfs root + exFAT data partition design.
   - Do not introduce systemd, heavyweight compositors (Wayland/X11), or sound servers (PipeWire/PulseAudio) unless explicitly approved. Keep userland strictly to `banana-ui` and `bananad`.

3. **Performance & Efficiency:**
   - Keep CPU composition and rendering overhead minimal. Given RK3568/RK3566 constraints, optimize for low-latency frame presentation and efficient damage-diffing.

4. **Code Quality:**
   - Write clean, idiomatic C++17 for UI components (`ui/src/`) and clean POSIX C for system daemons (`system/bananad/`).
   - Validate changes locally using the host test harness (`make test`) before proposing commits.
