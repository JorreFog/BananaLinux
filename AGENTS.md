```markdown
# Banana Pocket Agent Rules & Constraints

1. **Hardware Scope (Strict):**
   - All code, scripts, UI components, and driver hooks must target **ONLY** the Retroid Pocket Classic (Qualcomm Snapdragon G1 Gen 2 / SM4450, single 1240x1080 AMOLED panel, MCU gamepad, DRM/KMS + Freedreno).
   - Never introduce multi-device abstractions, generic Linux desktop packages, or unsupported hardware drivers.

2. **Architecture & Minimalism:**
   - Adhere strictly to the read-only squashfs root + exFAT data partition design.
   - Do not introduce systemd, heavyweight compositors (Wayland/X11), or sound servers (PipeWire/PulseAudio) unless explicitly approved. Keep userland strictly to `banana-ui` and `bananad`.

3. **Performance & Efficiency:**
   - Keep CPU composition and rendering overhead minimal. Given the SM4450's big.LITTLE cores, optimize for low-latency frame presentation and efficient damage-diffing.

4. **Code Quality:**
   - Write clean, idiomatic C++17 for UI components (`ui/src/`) and clean POSIX C for system daemons (`system/bananad/`).
   - Validate changes locally using the host test harness (`make test`) before proposing commits.
