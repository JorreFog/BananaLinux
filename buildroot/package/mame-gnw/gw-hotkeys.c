/*
 * gw-hotkeys PID: the RG DS's F button for the Game & Watch emulator (MAME, which doesn't see F), run beside it by
 * run-gw. Reads the pad (never grabs it: bananad reads it too, for the volume keys and F + volume):
 *   F + Start    quit: SIGTERM (MAME's patched SDL_QUIT saves and exits), SIGKILL if it's still there after 5 s
 *   F on its own pressed and released with no other key in between: SIGUSR1 (MAME's patch opens or closes its menu)
 *   L2           swap the panels: SIGUSR2 (MAME's patch draws the window's halves swapped)
 *   F + L2       a screenshot, as in the DS emulators: SIGWINCH (MAME's patch saves the view on the panels)
 * Exits when PID does.
 */
#include <fcntl.h>
#include <linux/input.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

/* the pad: the input device whose name has "joypad" in it (rocknix-singleadc-joypad, retrogame_joypad) */
static int open_pad(void)
{
    for (int i = 0; i < 32; i++) {
        char path[32], name[128] = "";
        snprintf(path, sizeof(path), "/dev/input/event%d", i);
        int fd = open(path, O_RDONLY | O_CLOEXEC);
        if (fd < 0) {
            continue;
        }
        ioctl(fd, EVIOCGNAME(sizeof(name)), name);
        if (strstr(name, "joypad")) {
            fprintf(stderr, "gw-hotkeys: %s (%s)\n", path, name);
            return fd;
        }
        close(fd);
    }
    return -1;
}

static void quit(pid_t pid)
{
    fprintf(stderr, "gw-hotkeys: F + Start, SIGTERM to %d\n", (int)pid);
    kill(pid, SIGTERM);
    for (int i = 0; i < 50 && kill(pid, 0) == 0; i++) {
        usleep(100000);
    }
    if (kill(pid, 0) == 0) {
        fprintf(stderr, "gw-hotkeys: %d still running, SIGKILL\n", (int)pid);
        kill(pid, SIGKILL);
    }
}

int main(int argc, char **argv)
{
    if (argc != 2) {
        fprintf(stderr, "usage: gw-hotkeys PID\n");
        return 2;
    }
    pid_t pid = (pid_t)atoi(argv[1]);
    int fd = open_pad();
    if (fd < 0) {
        fprintf(stderr, "gw-hotkeys: no joypad\n");
        return 1;
    }
    int f_down = 0, start_down = 0, f_alone = 0;
    struct pollfd p = { .fd = fd, .events = POLLIN };
    while (kill(pid, 0) == 0) {
        /* wake every second to notice the emulator has gone */
        if (poll(&p, 1, 1000) <= 0) {
            continue;
        }
        struct input_event ev;
        if (read(fd, &ev, sizeof(ev)) != sizeof(ev)) {
            break;
        }
        if (ev.type != EV_KEY || ev.value == 2) {
            continue;
        }
        if (ev.code == BTN_MODE) {
            f_down = ev.value != 0;
            if (f_down) {
                f_alone = 1;
            } else if (f_alone) {
                kill(pid, SIGUSR1);
            }
        } else {
            /* any other key while F is held (Start, the volume keys...): F was part of a combination */
            if (ev.value) {
                f_alone = 0;
            }
            if (ev.code == BTN_START) {
                start_down = ev.value != 0;
            } else if (ev.code == BTN_TL2 && ev.value) {
                kill(pid, f_down ? SIGWINCH : SIGUSR2);
            }
        }
        if (f_down && start_down) {
            quit(pid);
            return 0;
        }
    }
    return 0;
}
