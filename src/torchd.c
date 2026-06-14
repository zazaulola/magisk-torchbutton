/*
 * torchd — Magisk userspace daemon.
 *
 * Grabs the Power-button input device (EVIOCGRAB), implements its own
 * long-press detection, and re-injects events through uinput.
 *
 * Behaviour:
 *   - Flashlight ON  + long press  -> turn flashlight OFF (any screen state)
 *   - Flashlight OFF + long press + screen off/locked -> turn flashlight ON
 *   - Flashlight OFF + long press + screen on & unlocked -> system power dialog
 *   - Short press is forwarded as a normal short Power press.
 *
 * Build with the Android NDK (see src/build.sh).
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <dirent.h>
#include <signal.h>
#include <time.h>
#include <poll.h>
#include <ctype.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <linux/input.h>
#include <linux/uinput.h>

#define LOG_TAG "torchd"

static FILE *g_log = NULL;

static void log_init(const char *path) {
    if (path && *path) {
        g_log = fopen(path, "ae");   /* 'e' = O_CLOEXEC: don't leak into am child */
        if (g_log) setvbuf(g_log, NULL, _IOLBF, 0);
    }
    if (!g_log) g_log = stderr;
}

static void logmsg(const char *fmt, ...) {
    time_t t = time(NULL);
    struct tm tm;
    localtime_r(&t, &tm);
    char ts[32];
    strftime(ts, sizeof(ts), "%Y-%m-%d %H:%M:%S", &tm);
    fprintf(g_log, "%s [%s] ", ts, LOG_TAG);
    va_list ap;
    va_start(ap, fmt);
    vfprintf(g_log, fmt, ap);
    va_end(ap);
    fputc('\n', g_log);
    fflush(g_log);
}

/* ---------------------------------------------------------------------- */
/* Config                                                                  */
/* ---------------------------------------------------------------------- */

static int  g_long_press_ms      = 400;   /* shorter than system's ~500ms so we can cancel */
static int  g_verbose            = 0;     /* set via TORCHD_VERBOSE=1 */
static int  g_torch_brightness   = 255;   /* value written to torch sysfs (sysfs backend) */
static char g_torch_path[256]    = "";    /* sysfs torch node; empty = use APK backend */
static char g_torch_switch_path[256] = "";
static char g_backlight_path[256]= "";

/* When we can't write to sysfs (e.g. Pixel/Tensor — torch is owned by Camera
   HAL), we fall back to the companion APK. The APK tracks the *real* torch
   state via CameraManager.registerTorchCallback() and mirrors it into
   files/torch_state, so we notice when something else (e.g. the system
   flashlight QS tile) toggles the LED. We read that file; g_torch_state is
   only a fallback for when the file doesn't exist yet. */
static char g_torch_pkg[128]        = "me.nogrep.torchbutton";
static int  g_torch_state           = 0;  /* in-memory fallback: 0=off 1=on */
/* Device-encrypted storage (/data/user_de/0/...): readable by the magisk daemon
   both normally and before the first unlock (BFU), which credential-encrypted
   /data/data is not. The APK writes the same path via
   createDeviceProtectedStorageContext(). */
static char g_torch_state_file[256] = "/data/user_de/0/me.nogrep.torchbutton/files/torch_state";
static int  g_use_apk_backend       = 0;  /* set when discover_torch finds no sysfs */

/* Enable / disable: the companion APK writes "0" or "1" into a file that the
   daemon polls. Default-on if the file is missing (fresh install). When
   disabled, the daemon goes into transparent-passthrough mode — all events
   are forwarded immediately, identical to having no daemon at all. */
static char g_enable_file[256]   = "/data/user_de/0/me.nogrep.torchbutton/files/enabled";
static int  g_enabled            = 1;
static long long g_enabled_checked_ms = 0;
#define ENABLED_POLL_MS 500

static volatile sig_atomic_t g_running = 1;

static void on_signal(int sig) { (void)sig; g_running = 0; }

/* ---------------------------------------------------------------------- */
/* Time                                                                    */
/* ---------------------------------------------------------------------- */

static long long now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long long)ts.tv_sec * 1000LL + ts.tv_nsec / 1000000LL;
}

/* ---------------------------------------------------------------------- */
/* Enable / disable                                                        */
/* ---------------------------------------------------------------------- */

/* Read the enable flag from disk; cached for ENABLED_POLL_MS to avoid
   hammering the filesystem on every event. Default: enabled (so a fresh
   install behaves like before the user has even opened the helper app). */
static int check_enabled(void) {
    long long now = now_ms();
    if (now - g_enabled_checked_ms < ENABLED_POLL_MS) {
        return g_enabled;
    }
    g_enabled_checked_ms = now;

    FILE *f = fopen(g_enable_file, "r");
    if (!f) {
        if (g_enabled != 1) {
            logmsg("enable file missing (%s) — defaulting to ENABLED", g_enable_file);
        }
        g_enabled = 1;
        return g_enabled;
    }
    int v = 1;
    if (fscanf(f, "%d", &v) != 1) v = 1;
    fclose(f);
    int new_state = (v != 0) ? 1 : 0;
    if (new_state != g_enabled) {
        logmsg("torchbutton %s (via %s)",
               new_state ? "ENABLED" : "DISABLED (passthrough)", g_enable_file);
    }
    g_enabled = new_state;
    return g_enabled;
}

/* ---------------------------------------------------------------------- */
/* Torch path discovery                                                    */
/* ---------------------------------------------------------------------- */

static int file_writable(const char *path) {
    return access(path, W_OK) == 0;
}

static int try_torch_candidate(const char *brightness_path) {
    if (!file_writable(brightness_path)) return 0;
    strncpy(g_torch_path, brightness_path, sizeof(g_torch_path) - 1);
    g_torch_path[sizeof(g_torch_path) - 1] = '\0';

    /* Look for a sibling "switch" or "torch_enable" file. */
    char dir[256];
    strncpy(dir, brightness_path, sizeof(dir) - 1);
    dir[sizeof(dir) - 1] = '\0';
    char *slash = strrchr(dir, '/');
    if (slash) *slash = '\0';

    const char *switches[] = { "switch", "torch_enable", "enable", NULL };
    for (int i = 0; switches[i]; i++) {
        char p[512];
        snprintf(p, sizeof(p), "%s/%s", dir, switches[i]);
        if (file_writable(p)) {
            strncpy(g_torch_switch_path, p, sizeof(g_torch_switch_path) - 1);
            g_torch_switch_path[sizeof(g_torch_switch_path) - 1] = '\0';
            break;
        }
    }
    return 1;
}

static int discover_torch(void) {
    if (g_torch_path[0] && file_writable(g_torch_path)) return 0;

    /* Common explicit paths first. Roughly ordered: Pixel/Tensor, Qualcomm,
       MediaTek, others. */
    static const char *known[] = {
        /* Pixel / Tensor */
        "/sys/class/leds/led:torch_0/brightness",
        "/sys/class/leds/led:torch_1/brightness",
        "/sys/class/leds/led:torch_2/brightness",
        "/sys/class/leds/led:flash_torch/brightness",
        "/sys/class/leds/flashlight_torch/brightness",
        /* Qualcomm */
        "/sys/class/leds/torch-light0/brightness",
        "/sys/class/leds/torch-light1/brightness",
        "/sys/class/leds/torch-light/brightness",
        "/sys/class/leds/led:flash_0/brightness",
        "/sys/class/leds/led:flash_1/brightness",
        /* MediaTek */
        "/sys/class/leds/flashlight/brightness",
        /* Generic */
        "/sys/class/leds/torch/brightness",
        "/sys/class/leds/torch-front/brightness",
        "/sys/class/leds/spotlight/brightness",
        "/sys/class/flashlight/flashlight/brightness",
        "/sys/class/flashlight_core/flashlight/brightness",
        NULL,
    };
    for (int i = 0; known[i]; i++) {
        if (try_torch_candidate(known[i])) return 0;
    }

    /* Scan /sys/class/leds for any directory whose name looks like a torch.
       Two passes so a real "torch" node always wins over a "flash"/"spot" one,
       and skip names that merely contain those substrings but aren't the torch
       (e.g. button-backlight, charging LED, keyboard/indicator LEDs). */
    static const char *deny[] = {
        "backlight", "button", "keyboard", "kbd", "indicator",
        "charg", "battery", "notification", "lcd", "wled", NULL
    };
    for (int pass = 0; pass < 2; pass++) {
        DIR *d = opendir("/sys/class/leds");
        if (!d) break;
        struct dirent *e;
        while ((e = readdir(d))) {
            if (e->d_name[0] == '.') continue;
            const char *n = e->d_name;
            int denied = 0;
            for (int i = 0; deny[i]; i++) if (strcasestr(n, deny[i])) { denied = 1; break; }
            if (denied) continue;
            int is_torch = strcasestr(n, "torch") != NULL;
            int is_flashlike = strcasestr(n, "flash") != NULL || strcasestr(n, "spot") != NULL;
            /* pass 0: only real torch nodes; pass 1: flash/spot fallbacks. */
            if (pass == 0 ? !is_torch : !(is_flashlike && !is_torch)) continue;
            char p[256];
            snprintf(p, sizeof(p), "/sys/class/leds/%s/brightness", n);
            if (try_torch_candidate(p)) { closedir(d); return 0; }
        }
        closedir(d);
    }

    return -1;
}

static int torch_read(void) {
    if (g_use_apk_backend) {
        /* Source of truth: the file the APK keeps in sync with the real LED
           state via its torch callback. Fall back to our last-known value if
           the file isn't there yet (APK not installed / never ran). */
        FILE *f = fopen(g_torch_state_file, "r");
        if (f) {
            int v = 0;
            int ok = fscanf(f, "%d", &v) == 1;
            fclose(f);
            if (ok) {
                g_torch_state = (v != 0) ? 1 : 0;
                return g_torch_state;
            }
        }
        return g_torch_state;
    }
    if (!g_torch_path[0]) return -1;
    FILE *f = fopen(g_torch_path, "r");
    if (!f) return -1;
    int v = 0;
    int ok = fscanf(f, "%d", &v) == 1;
    fclose(f);
    return ok ? (v > 0 ? 1 : 0) : -1;
}

static void torch_write_apk(int on) {
    /* Target the receiver by explicit component so it can stay exported=false
       (no other app can reach it). We run as root, so `am` is allowed to
       deliver to a non-exported component. */
    char component[160];
    snprintf(component, sizeof(component), "%s/.TorchReceiver", g_torch_pkg);

    /* fork+exec `am broadcast` so we don't block the main loop waiting for
       the child. The user is still holding the button anyway, but we want to
       keep reading input events (release detection) without latency. */
    pid_t pid = fork();
    if (pid < 0) {
        logmsg("fork failed: %s", strerror(errno));
        return;
    }
    if (pid == 0) {
        /* Child: redirect stdio away from the daemon log and exec am. */
        int devnull = open("/dev/null", O_RDWR | O_CLOEXEC);
        if (devnull >= 0) {
            dup2(devnull, 0); dup2(devnull, 1); dup2(devnull, 2);
            if (devnull > 2) close(devnull);
        }
        const char *state = on ? "on" : "off";
        execlp("am", "am", "broadcast",
               "-a", "me.nogrep.torchbutton.SET",
               "--es", "state", state,
               "-n", component,
               "--include-stopped-packages",
               (char *)NULL);
        _exit(127);
    }
    /* Don't wait for child; reap any zombies later. */
}

static void torch_write(int on) {
    if (g_use_apk_backend) {
        torch_write_apk(on);
        g_torch_state = on ? 1 : 0;
        return;
    }
    if (!g_torch_path[0]) return;
    int val = on ? g_torch_brightness : 0;
    int fd = open(g_torch_path, O_WRONLY | O_CLOEXEC);
    if (fd >= 0) {
        char buf[16];
        int n = snprintf(buf, sizeof(buf), "%d", val);
        if (n > 0 && n < (int)sizeof(buf) && write(fd, buf, n) < 0) {
            logmsg("torch write failed: %s", strerror(errno));
        }
        close(fd);
    } else {
        logmsg("open torch path failed: %s (%s)", g_torch_path, strerror(errno));
    }
    if (g_torch_switch_path[0]) {
        int sfd = open(g_torch_switch_path, O_WRONLY);
        if (sfd >= 0) {
            const char *s = on ? "1" : "0";
            (void)write(sfd, s, 1);
            close(sfd);
        }
    }
}

/* ---------------------------------------------------------------------- */
/* Screen / lock detection                                                 */
/* ---------------------------------------------------------------------- */

static int read_int_file(const char *path) {
    FILE *f = fopen(path, "r");
    if (!f) return -1;
    int v = -1;
    if (fscanf(f, "%d", &v) != 1) v = -1;
    fclose(f);
    return v;
}

static int discover_backlight(void) {
    if (g_backlight_path[0] && access(g_backlight_path, R_OK) == 0) return 0;
    static const char *candidates[] = {
        "/sys/class/leds/lcd-backlight/brightness",
        "/sys/class/backlight/panel0-backlight/brightness",
        "/sys/class/backlight/panel/brightness",
        NULL,
    };
    for (int i = 0; candidates[i]; i++) {
        if (access(candidates[i], R_OK) == 0) {
            strncpy(g_backlight_path, candidates[i], sizeof(g_backlight_path) - 1);
            g_backlight_path[sizeof(g_backlight_path) - 1] = '\0';
            return 0;
        }
    }
    /* Scan /sys/class/backlight. */
    DIR *d = opendir("/sys/class/backlight");
    if (d) {
        struct dirent *e;
        while ((e = readdir(d))) {
            if (e->d_name[0] == '.') continue;
            char p[256];
            snprintf(p, sizeof(p), "/sys/class/backlight/%s/brightness", e->d_name);
            if (access(p, R_OK) == 0) {
                strncpy(g_backlight_path, p, sizeof(g_backlight_path) - 1);
                g_backlight_path[sizeof(g_backlight_path) - 1] = '\0';
                closedir(d);
                return 0;
            }
        }
        closedir(d);
    }
    return -1;
}

static int is_screen_on(void) {
    if (g_backlight_path[0]) {
        int v = read_int_file(g_backlight_path);
        if (v >= 0) return v > 0;
    }
    int v = read_int_file("/sys/class/graphics/fb0/blank");
    if (v >= 0) return v == 0;
    return 1; /* fail-open: treat as on, so we don't accidentally light up */
}

/*
 * Lock detection: spawn `dumpsys window` and look for known markers.
 * Slow (~50-200 ms), but only runs once per long-press.
 *
 * dumpsys output evolved across Android versions, so we accept several shapes:
 *
 *   - Legacy single-line: mShowingLockscreen=true / isStatusBarKeyguard=true
 *   - Android 13+:  "KeyguardServiceDelegate:" or "KeyguardController:" section
 *     header followed by indented "showing=true" / "mShowing=true" line.
 *
 * The multi-line case is handled by tracking when we're inside a "Keyguard"
 * section (subsequent indented lines belong to it) and applying the patterns
 * only there. This matches what Android 14/15/16 emit.
 */
static int is_keyguard_locked(void) {
    /* Bounded with `timeout` (toybox) so a wedged WindowManager can never block
       the input loop — a hung dumpsys would otherwise leave the Power button
       dead. On timeout/failure we fail open (return 0 = not locked), which
       routes a screen-on long-press to the system power dialog rather than
       silently turning the torch on. */
    FILE *p = popen("timeout 1 dumpsys window 2>/dev/null", "r");
    if (!p) return 0;
    char line[1024];
    int locked = 0;
    int in_keyguard_section = 0;
    while (fgets(line, sizeof(line), p)) {
        /* Legacy single-line markers. */
        if ((strstr(line, "mShowingLockscreen") && strstr(line, "true")) ||
            (strstr(line, "isStatusBarKeyguard") && strstr(line, "true")) ||
            (strstr(line, "mDreamingLockscreen") && strstr(line, "true")))
        {
            locked = 1;
        }

        /* Section header — both old and new names. */
        if (strstr(line, "KeyguardServiceDelegate") ||
            strstr(line, "KeyguardController") ||
            strstr(line, "KeyguardStateMonitor"))
        {
            in_keyguard_section = 1;
            continue;
        }
        /* Leave the section when indentation drops back to column 0. */
        if (in_keyguard_section && line[0] != ' ' && line[0] != '\t' && line[0] != '\n') {
            in_keyguard_section = 0;
        }
        if (in_keyguard_section) {
            if (strstr(line, "showing=true")           ||
                strstr(line, "mShowing=true")          ||
                strstr(line, "mShowingAndNotOccluded=true") ||
                strstr(line, "mIsShowing=true"))
            {
                locked = 1;
            }
        }
    }
    pclose(p);
    return locked;
}

/* ---------------------------------------------------------------------- */
/* Input device discovery                                                  */
/* ---------------------------------------------------------------------- */

#define BITS_PER_LONG (sizeof(long) * 8)
#define NBITS(x) ((((x) - 1) / BITS_PER_LONG) + 1)
#define TEST_BIT(arr, b) (((arr)[(b) / BITS_PER_LONG] >> ((b) % BITS_PER_LONG)) & 1UL)

static int find_power_device(char *out, size_t out_size) {
    DIR *d = opendir("/dev/input");
    if (!d) return -1;
    char best_path[256] = "";
    int best_keycount = 0; /* prefer a device that has the FEWEST keys (likely the
                              dedicated gpio-keys/power node, not a full keyboard) */
    int best_eventnum = -1;
    struct dirent *e;
    while ((e = readdir(d))) {
        if (strncmp(e->d_name, "event", 5) != 0) continue;
        int evn = atoi(e->d_name + 5);
        char path[300];
        snprintf(path, sizeof(path), "/dev/input/%s", e->d_name);
        int fd = open(path, O_RDONLY | O_CLOEXEC);
        if (fd < 0) continue;
        unsigned long key_bits[NBITS(KEY_MAX)];
        memset(key_bits, 0, sizeof(key_bits));
        int has_power = ioctl(fd, EVIOCGBIT(EV_KEY, sizeof(key_bits)), key_bits) >= 0 &&
                        TEST_BIT(key_bits, KEY_POWER);
        close(fd);
        if (!has_power) continue;

        int count = 0;
        for (int i = 0; i < KEY_MAX; i++) if (TEST_BIT(key_bits, i)) count++;
        if (g_verbose) logmsg("candidate %s: KEY_POWER, %d keys", path, count);

        /* Deterministic pick: fewest keys, ties broken by lowest eventN so the
           choice doesn't depend on readdir() order. */
        int better = best_path[0] == '\0' ||
                     count < best_keycount ||
                     (count == best_keycount && evn < best_eventnum);
        if (better) {
            best_keycount = count;
            best_eventnum = evn;
            strncpy(best_path, path, sizeof(best_path) - 1);
            best_path[sizeof(best_path) - 1] = '\0';
        }
    }
    closedir(d);
    if (best_path[0] == '\0') return -1;
    strncpy(out, best_path, out_size - 1);
    out[out_size - 1] = '\0';
    return 0;
}

/* ---------------------------------------------------------------------- */
/* uinput mirror device                                                    */
/* ---------------------------------------------------------------------- */

static int create_uinput_mirror(int src_fd) {
    int fd = open("/dev/uinput", O_WRONLY | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0) {
        logmsg("open /dev/uinput failed: %s", strerror(errno));
        return -1;
    }

    /* Copy event-type bits from source. */
    unsigned long ev_bits[NBITS(EV_MAX)];
    memset(ev_bits, 0, sizeof(ev_bits));
    ioctl(src_fd, EVIOCGBIT(0, sizeof(ev_bits)), ev_bits);
    for (int t = 0; t < EV_MAX; t++) {
        if (TEST_BIT(ev_bits, t)) {
            if (ioctl(fd, UI_SET_EVBIT, t) < 0) {
                /* not all event types are settable; ignore */
            }
        }
    }
    /* Always enable EV_KEY/EV_SYN even if source didn't report them. */
    ioctl(fd, UI_SET_EVBIT, EV_KEY);
    ioctl(fd, UI_SET_EVBIT, EV_SYN);

    /* Copy key bits. */
    unsigned long key_bits[NBITS(KEY_MAX)];
    memset(key_bits, 0, sizeof(key_bits));
    ioctl(src_fd, EVIOCGBIT(EV_KEY, sizeof(key_bits)), key_bits);
    int keys_set = 0;
    for (int k = 0; k < KEY_MAX; k++) {
        if (TEST_BIT(key_bits, k)) {
            ioctl(fd, UI_SET_KEYBIT, k);
            keys_set++;
        }
    }
    /* Guarantee KEY_POWER is available even if source had nothing. */
    ioctl(fd, UI_SET_KEYBIT, KEY_POWER);

    /* Copy switch bits. */
    unsigned long sw_bits[NBITS(SW_MAX)];
    memset(sw_bits, 0, sizeof(sw_bits));
    if (ioctl(src_fd, EVIOCGBIT(EV_SW, sizeof(sw_bits)), sw_bits) >= 0) {
        for (int s = 0; s < SW_MAX; s++) {
            if (TEST_BIT(sw_bits, s)) ioctl(fd, UI_SET_SWBIT, s);
        }
    }

    struct uinput_user_dev uidev;
    memset(&uidev, 0, sizeof(uidev));
    snprintf(uidev.name, UINPUT_MAX_NAME_SIZE, "torchd-virtual");
    uidev.id.bustype = BUS_VIRTUAL;
    uidev.id.vendor  = 0x1209;
    uidev.id.product = 0x70de;
    uidev.id.version = 1;
    if (write(fd, &uidev, sizeof(uidev)) != sizeof(uidev)) {
        logmsg("uinput dev write failed: %s", strerror(errno));
        close(fd);
        return -1;
    }
    if (ioctl(fd, UI_DEV_CREATE) < 0) {
        logmsg("UI_DEV_CREATE failed: %s", strerror(errno));
        close(fd);
        return -1;
    }
    logmsg("uinput mirror created (%d key codes copied)", keys_set);
    return fd;
}

static void emit(int ufd, int type, int code, int value) {
    struct input_event ev;
    memset(&ev, 0, sizeof(ev));
    ev.type = type;
    ev.code = code;
    ev.value = value;
    (void)write(ufd, &ev, sizeof(ev));
}

static void emit_syn(int ufd) {
    emit(ufd, EV_SYN, SYN_REPORT, 0);
}

/* ---------------------------------------------------------------------- */
/* Main loop                                                               */
/* ---------------------------------------------------------------------- */

enum power_state {
    PS_IDLE = 0,      /* no Power held */
    PS_LIVE,          /* Power pressed, forwarded immediately, awaiting decision */
    PS_FORWARDED,     /* long-press decided as "let system handle" */
    PS_HANDLED,       /* long-press handled by us; release already sent */
};

static const char *state_name(enum power_state s) {
    switch (s) {
        case PS_IDLE: return "IDLE";
        case PS_LIVE: return "LIVE";
        case PS_FORWARDED: return "FORWARDED";
        case PS_HANDLED: return "HANDLED";
    }
    return "?";
}

static int run_session(void);

int main(int argc, char **argv) {
    /* CLI / env config */
    const char *log_path = getenv("TORCHD_LOG");
    if (!log_path) log_path = "/data/local/tmp/torchd.log";
    log_init(log_path);

    const char *env_thr = getenv("TORCHD_THRESHOLD_MS");
    if (env_thr && atoi(env_thr) > 100) g_long_press_ms = atoi(env_thr);

    const char *env_br = getenv("TORCHD_BRIGHTNESS");
    if (env_br && atoi(env_br) > 0) g_torch_brightness = atoi(env_br);

    const char *env_torch = getenv("TORCHD_TORCH_PATH");
    if (env_torch && env_torch[0]) {
        strncpy(g_torch_path, env_torch, sizeof(g_torch_path) - 1);
        g_torch_path[sizeof(g_torch_path) - 1] = '\0';
    }
    if (argc >= 2 && argv[1][0]) {
        strncpy(g_torch_path, argv[1], sizeof(g_torch_path) - 1);
        g_torch_path[sizeof(g_torch_path) - 1] = '\0';
    }

    const char *env_pkg = getenv("TORCHD_PKG");
    if (env_pkg && env_pkg[0]) {
        strncpy(g_torch_pkg, env_pkg, sizeof(g_torch_pkg) - 1);
        g_torch_pkg[sizeof(g_torch_pkg) - 1] = '\0';
    }

    const char *env_backend = getenv("TORCHD_BACKEND");
    if (env_backend && strcmp(env_backend, "apk") == 0) {
        g_use_apk_backend = 1;
        g_torch_path[0] = '\0';
    }

    const char *env_v = getenv("TORCHD_VERBOSE");
    if (env_v && atoi(env_v) > 0) g_verbose = 1;

    const char *env_enable = getenv("TORCHD_ENABLE_FILE");
    if (env_enable && env_enable[0]) {
        strncpy(g_enable_file, env_enable, sizeof(g_enable_file) - 1);
        g_enable_file[sizeof(g_enable_file) - 1] = '\0';
    }

    const char *env_tstate = getenv("TORCHD_TORCH_STATE_FILE");
    if (env_tstate && env_tstate[0]) {
        strncpy(g_torch_state_file, env_tstate, sizeof(g_torch_state_file) - 1);
        g_torch_state_file[sizeof(g_torch_state_file) - 1] = '\0';
    }

    signal(SIGINT,  on_signal);
    signal(SIGTERM, on_signal);
    signal(SIGPIPE, SIG_IGN);

    if (g_use_apk_backend) {
        logmsg("APK backend forced via TORCHD_BACKEND=apk (pkg=%s)", g_torch_pkg);
    } else if (discover_torch() != 0) {
        /* No writable sysfs LED node (Pixel/Tensor, some other modern devices).
           Fall back to broadcasting an intent to the companion APK, which
           uses CameraManager.setTorchMode(). */
        g_use_apk_backend = 1;
        logmsg("no sysfs torch — using APK backend (pkg=%s, state=%s)",
               g_torch_pkg, g_torch_state_file);
    } else {
        logmsg("torch brightness=%s  switch=%s",
               g_torch_path, g_torch_switch_path[0] ? g_torch_switch_path : "(none)");
    }

    /* Reap zombie am-broadcast children automatically. */
    signal(SIGCHLD, SIG_IGN);

    if (discover_backlight() != 0) {
        logmsg("warning: backlight sysfs not found; will fall back to fb0/blank");
    } else {
        logmsg("backlight=%s", g_backlight_path);
    }

    /* Acquire the input device and run the event loop. On a device error
       (hotplug / suspend-resume re-enumeration / read error) re-acquire in
       place instead of exiting, so the gesture survives without waiting for
       the watchdog's restart delay. Backs off, capped at 2s. */
    int retry_ms = 200;
    while (g_running) {
        int rc = run_session();
        if (rc == 0 || !g_running) break;   /* clean shutdown via signal */
        logmsg("session ended (rc=%d) — re-acquiring in %dms", rc, retry_ms);
        usleep(retry_ms * 1000);
        if (retry_ms < 2000) retry_ms *= 2;
    }
    logmsg("shutting down");
    return 0;
}

/* One acquire-and-run session. Grabs the Power device, mirrors it via uinput,
   and runs the event loop until either a clean shutdown (signal) -> returns 0,
   or a device/setup error -> returns >0 so main() can re-acquire. */
static int run_session(void) {
    char powerdev[256];
    if (find_power_device(powerdev, sizeof(powerdev)) != 0) {
        logmsg("could not find input device with KEY_POWER");
        return 3;
    }
    logmsg("power device=%s", powerdev);

    int src_fd = open(powerdev, O_RDONLY | O_CLOEXEC);
    if (src_fd < 0) {
        logmsg("open %s failed: %s", powerdev, strerror(errno));
        return 4;
    }
    if (ioctl(src_fd, EVIOCGRAB, 1) < 0) {
        logmsg("EVIOCGRAB failed: %s", strerror(errno));
        close(src_fd);
        return 5;
    }

    int ufd = create_uinput_mirror(src_fd);
    if (ufd < 0) {
        ioctl(src_fd, EVIOCGRAB, 0);
        close(src_fd);
        return 6;
    }

    logmsg("torchd ready (long_press_ms=%d, brightness=%d, enable_file=%s)",
           g_long_press_ms, g_torch_brightness, g_enable_file);

    int rc = 0;
    /*
     * State machine — always-buffer with chord exception:
     *
     * By default we do NOT forward the press at t=0. The system therefore
     * never sees a pressed Power key during our 0..threshold decision window,
     * which is what prevents Pixel (and any device with a sub-400ms long-press)
     * from pre-emptively opening the power menu that we then have to cancel.
     *
     * Exception — Power+Volume chord (screenshot, assistant, etc.):
     *   If any Volume key is already held when Power goes down, we forward
     *   the Power press immediately so the system's ScreenshotChord logic
     *   (which expects both keys held within ~150 ms) can fire. Same thing
     *   if a Volume key arrives while we're still buffering Power — we
     *   retroactively forward the buffered press.
     *
     *  - On release before long-press threshold:
     *      forwarded -> emit release (chord that didn't trigger)
     *      buffered  -> replay press + small hold + release (short tap)
     *
     *  - On long-press threshold:
     *      torch on  OR  screen off/locked   -> torch action;
     *                                           if we'd already forwarded
     *                                           (chord), emit release to
     *                                           cancel the system view.
     *      otherwise (screen on & unlocked & torch off)
     *                                        -> emit press NOW (if not
     *                                           already), system shows menu.
     *
     *  press_forwarded — we have an outstanding press in uinput.
     *  volume_held    — count of Vol-Up/Vol-Down currently pressed.
     */
    enum power_state state = PS_IDLE;
    long long press_started_ms = 0;
    int press_forwarded = 0;
    int volume_held = 0;

    while (g_running) {
        struct pollfd pfd = { .fd = src_fd, .events = POLLIN };
        int timeout = -1;
        if (state == PS_LIVE) {
            long long elapsed = now_ms() - press_started_ms;
            timeout = g_long_press_ms - (int)elapsed;
            if (timeout < 0) timeout = 0;
        }
        int pr = poll(&pfd, 1, timeout);
        if (pr < 0) {
            if (errno == EINTR) continue;
            logmsg("poll error: %s", strerror(errno));
            rc = 1;
            break;
        }
        if (pr > 0 && (pfd.revents & (POLLERR | POLLHUP | POLLNVAL))) {
            /* Device went away (suspend/resume re-enumeration, unplug). Bail so
               we re-acquire — without this, poll() would spin returning the
               error flag forever and peg a core. */
            logmsg("power device error: revents=0x%x — re-acquiring", pfd.revents);
            rc = 1;
            break;
        }

        /* Long-press fired with no release yet? */
        if (state == PS_LIVE && now_ms() - press_started_ms >= g_long_press_ms) {
            if (!check_enabled()) {
                /* Disabled. Normally the press was already forwarded at t=0,
                   but if it was buffered (enable flag flipped off between this
                   press and the threshold), forward it now so we don't leave an
                   orphan release with no matching press. */
                if (!press_forwarded) {
                    emit(ufd, EV_KEY, KEY_POWER, 1);
                    emit_syn(ufd);
                    press_forwarded = 1;
                }
                state = PS_FORWARDED;
                continue;
            }
            int torch_is_on = torch_read();
            int screen_on   = is_screen_on();
            int locked      = (!screen_on) ? 1 : is_keyguard_locked();

            logmsg("long-press: torch=%d screen_on=%d locked=%d fwd=%d",
                   torch_is_on, screen_on, locked, press_forwarded);

            if (torch_is_on == 1) {
                /* If a chord forwarded the press, we have to cancel it so the
                   system doesn't think the user is doing a long-press. */
                if (press_forwarded) {
                    emit(ufd, EV_KEY, KEY_POWER, 0);
                    emit_syn(ufd);
                }
                torch_write(0);
                state = PS_HANDLED;
            } else if (!screen_on || locked) {
                if (press_forwarded) {
                    emit(ufd, EV_KEY, KEY_POWER, 0);
                    emit_syn(ufd);
                }
                torch_write(1);
                state = PS_HANDLED;
            } else {
                /* Screen on & unlocked & torch off -> let the system show its
                   power dialog. Emit the press NOW (if not already forwarded
                   via a chord) so the system can start its long-press timer. */
                if (!press_forwarded) {
                    emit(ufd, EV_KEY, KEY_POWER, 1);
                    emit_syn(ufd);
                    press_forwarded = 1;
                }
                state = PS_FORWARDED;
            }
        }

        if (pr > 0 && (pfd.revents & POLLIN)) {
            struct input_event ev;
            ssize_t n = read(src_fd, &ev, sizeof(ev));
            if (n < (ssize_t)sizeof(ev)) {
                if (n < 0 && errno != EAGAIN && errno != EINTR) {
                    logmsg("read failed: %s — re-acquiring", strerror(errno));
                    rc = 1;
                    break;
                }
                continue;
            }

            if (ev.type == EV_KEY && ev.code == KEY_POWER) {
                if (ev.value == 1) {
                    /* Press. Default: buffer. Forward immediately if:
                       - the module is disabled (passthrough mode), or
                       - a Volume key is already held (Power+Vol chord —
                         screenshot, assistant — needs both keys within
                         ~150 ms of each other). */
                    press_started_ms = now_ms();
                    state = PS_LIVE;
                    int enabled = check_enabled();
                    if (!enabled || volume_held > 0) {
                        emit(ufd, EV_KEY, KEY_POWER, 1);
                        emit_syn(ufd);
                        press_forwarded = 1;
                    } else {
                        press_forwarded = 0;
                    }
                    if (g_verbose) logmsg("POWER press (vol_held=%d, fwd=%d, en=%d)",
                                          volume_held, press_forwarded, enabled);
                } else if (ev.value == 0) {
                    /* Release */
                    if (state == PS_LIVE) {
                        if (press_forwarded) {
                            emit(ufd, EV_KEY, KEY_POWER, 0);
                            emit_syn(ufd);
                        } else {
                            /* Short press while screen was off — replay as
                               a complete short press for the system. The tiny
                               sleep makes sure some OEMs that require a minimum
                               hold time still register it as a wake press. */
                            emit(ufd, EV_KEY, KEY_POWER, 1);
                            emit_syn(ufd);
                            usleep(40 * 1000);
                            emit(ufd, EV_KEY, KEY_POWER, 0);
                            emit_syn(ufd);
                        }
                    } else if (state == PS_FORWARDED) {
                        emit(ufd, EV_KEY, KEY_POWER, 0);
                        emit_syn(ufd);
                    }
                    /* PS_HANDLED already emitted release (or never forwarded); skip. */
                    if (g_verbose) {
                        long long dur = now_ms() - press_started_ms;
                        logmsg("POWER release (held %lldms, state was %s)",
                               dur, state_name(state));
                    }
                    state = PS_IDLE;
                    press_forwarded = 0;
                } else {
                    /* Auto-repeat (ev.value == 2). Only matters if we forwarded. */
                    if (press_forwarded) {
                        emit(ufd, ev.type, ev.code, ev.value);
                        emit_syn(ufd);
                    }
                }
            } else if (ev.type == EV_KEY &&
                       (ev.code == KEY_VOLUMEUP || ev.code == KEY_VOLUMEDOWN)) {
                /* Track Volume so we can detect a chord with Power. If a
                   Volume key goes down while Power is still buffered, the
                   user is starting a chord *now* — retroactively forward
                   the buffered Power press so the framework can match it. */
                if (ev.value == 1) {
                    volume_held++;
                    if (state == PS_LIVE && !press_forwarded) {
                        emit(ufd, EV_KEY, KEY_POWER, 1);
                        emit_syn(ufd);
                        press_forwarded = 1;
                        if (g_verbose) logmsg("POWER retro-forwarded (chord start)");
                    }
                } else if (ev.value == 0) {
                    if (volume_held > 0) volume_held--;
                }
                emit(ufd, ev.type, ev.code, ev.value);
            } else {
                /* Any other event the grabbed device produces -> forward verbatim. */
                emit(ufd, ev.type, ev.code, ev.value);
            }
        }
    }

    logmsg("session ending (rc=%d, state=%s)", rc, state_name(state));
    /* Best-effort: make sure we leave the system without a stuck press. */
    if (press_forwarded && (state == PS_LIVE || state == PS_FORWARDED)) {
        emit(ufd, EV_KEY, KEY_POWER, 0);
        emit_syn(ufd);
    }
    ioctl(ufd, UI_DEV_DESTROY);
    close(ufd);
    ioctl(src_fd, EVIOCGRAB, 0);
    close(src_fd);
    return rc;
}
