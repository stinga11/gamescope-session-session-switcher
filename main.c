/*
 * session-switcher.c
 *
 * Reimplementación nativa de steamos-session-select usando libsystemd
 * (D-Bus directo para las partes que lo justifican) en vez de encadenar
 * fork/exec a qdbus/loginctl repetidamente.
 *
 * Validado con round-trips completos gamescope <-> plasma en CachyOS
 * (plasmalogin) tras debug extenso de:
 *   - bug de caching de plasmalogin (requiere restart de servicio, no
 *     solo logout, para releer Session=)
 *   - condicion de carrera de liberacion de VT entre sesiones (settle delay)
 *   - WAYLAND_DISPLAY colgado entre sesiones (fix vive en
 *     gamescope-session.service, fuera de este binario)
 *
 * No reemplaza steam-set-session ni los fixes a nivel de systemd units;
 * solo reemplaza la capa de orquestacion de steamos-session-select.
 *
 * Compilar: ver build.sh
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <systemd/sd-bus.h>
#include <systemd/sd-login.h>

#define SETTLE_DELAY_SEC    2
#define POLL_INTERVAL_USEC  500000  /* 0.5s */

/* ------------------------------------------------------------------ */
/* Ejecucion de comandos via fork/execvp, sin shell intermedia        */
/* ------------------------------------------------------------------ */
static int safe_exec(char *const argv[]) {
    pid_t pid = fork();
    if (pid == -1) {
        perror("fork");
        return -1;
    }
    if (pid == 0) {
        execvp(argv[0], argv);
        _exit(127);  /* solo se alcanza si execvp falla */
    }
    int status;
    if (waitpid(pid, &status, 0) == -1) {
        perror("waitpid");
        return -1;
    }
    return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}

/* ------------------------------------------------------------------ */
/* Espera bloqueante a que la sesion actual desaparezca de logind     */
/* (consulta nativa via sd-login, sin forkear loginctl)               */
/* ------------------------------------------------------------------ */
static void wait_for_session_end(const char *session_id) {
    if (!session_id) return;
    while (sd_session_is_active(session_id) > 0) {
        usleep(POLL_INTERVAL_USEC);
    }
}

/* ------------------------------------------------------------------ */
/* Espera bloqueante a que un unit --user quede inactivo              */
/* (via systemctl: simple y ya validado en la practica; la version    */
/*  100% D-Bus manual demostro ser fragil de escribir bien a mano)    */
/* ------------------------------------------------------------------ */
static void wait_for_user_unit_inactive(const char *unit) {
    char *cmd[] = {"systemctl", "--user", "is-active", "--quiet",
                   (char *)unit, NULL};
    while (safe_exec(cmd) == 0) {
        usleep(POLL_INTERVAL_USEC);
    }
}

/* ------------------------------------------------------------------ */
/* Llama org.kde.Shutdown.logout via D-Bus de sesion (sin fork)       */
/* ------------------------------------------------------------------ */
static int kde_logout(sd_bus *bus) {
    if (!bus) return -1;

    sd_bus_error error = SD_BUS_ERROR_NULL;
    sd_bus_message *reply = NULL;

    int r = sd_bus_call_method(bus,
                                "org.kde.Shutdown",
                                "/Shutdown",
                                "org.kde.Shutdown",
                                "logout",
                                &error, &reply, NULL);

    if (r < 0) {
        fprintf(stderr, "kde_logout nativo fallo: %s\n",
                error.message ? error.message : strerror(-r));
    }

    sd_bus_error_free(&error);
    if (reply) sd_bus_message_unref(reply);
    return r;
}

/* ------------------------------------------------------------------ */
/* Restaura mandos ocultos si existen (necesita shell por el glob *)  */
/* ------------------------------------------------------------------ */
static void handle_hidden_controllers(void) {
    if (access("/dev/input/.hidden", F_OK) != 0) return;

    fprintf(stderr, "Unhide hidden controller devices as needed\n");
    int rc = system(
        "pkexec mv /dev/input/.hidden/* /dev/input && "
        "pkexec rm -r /dev/input/.hidden"
    );
    (void)rc;
}

static void print_usage(const char *prog) {
    fprintf(stderr,
        "Usage: %s <mode>\n\n"
        "Modes:\n"
        "  persistent   Enable persistent session mode.\n"
        "               The system will remember the last session you used\n"
        "               and boot into it next time.\n"
        "  oneshot      Restore default oneshot behaviour.\n"
        "               Every fresh boot starts in Gamescope first.\n"
        "  gamescope    Immediately switch to the Gamescope session.\n"
        "  plasma       Immediately switch to the desktop session.\n",
        prog);
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        print_usage(argv[0]);
        return 1;
    }

    const char *mode = argv[1];

    char *session_id = NULL;
    if (sd_pid_get_session(0, &session_id) < 0) {
        session_id = NULL;
    }

    sd_bus *user_bus = NULL;
    sd_bus_open_user(&user_bus);  /* si falla, user_bus queda NULL; se maneja abajo */

    if (strcmp(mode, "persistent") == 0) {
        char *cmd1[] = {"systemctl", "--user", "mask",
                         "cachyos-gamescope-autologin.service", NULL};
        char *cmd2[] = {"pkexec", "/usr/lib/steamos/steam-set-session",
                         "plasma.desktop", NULL};
        safe_exec(cmd1);
        safe_exec(cmd2);

    } else if (strcmp(mode, "oneshot") == 0) {
        char *cmd1[] = {"systemctl", "--user", "unmask",
                         "cachyos-gamescope-autologin.service", NULL};
        char *cmd2[] = {"pkexec", "/usr/lib/steamos/steam-set-session",
                         "gamescope-session.desktop", NULL};
        safe_exec(cmd1);
        safe_exec(cmd2);

    } else if (strcmp(mode, "gamescope") == 0) {
        char *cmd1[] = {"pkexec", "/usr/lib/steamos/steam-set-session",
                         "gamescope-session.desktop", NULL};
        safe_exec(cmd1);

        if (kde_logout(user_bus) < 0) {
            /* qdbus6 es el binario en Arch/CachyOS; qdbus-qt6 es el
               equivalente en Fedora. Se intenta primero el de Arch
               (donde se valido este binario) y se cae al de Fedora
               por si el binario se usa en esa distro. */
            char *fallback_arch[] = {"qdbus6", "org.kde.Shutdown",
                                      "/Shutdown", "org.kde.Shutdown.logout", NULL};
            if (safe_exec(fallback_arch) != 0) {
                char *fallback_fedora[] = {"qdbus-qt6", "org.kde.Shutdown",
                                            "/Shutdown", "org.kde.Shutdown.logout", NULL};
                safe_exec(fallback_fedora);
            }
        }

        wait_for_session_end(session_id);
        sleep(SETTLE_DELAY_SEC);  /* colchon para liberacion de VT */

        char *cmd_restart[] = {"sudo", "systemctl", "restart",
                                "plasmalogin.service", NULL};
        safe_exec(cmd_restart);

    } else if (strcmp(mode, "plasma") == 0) {
        char *cmd1[] = {"pkexec", "/usr/lib/steamos/steam-set-session",
                         "plasma.desktop", NULL};
        char *cmd2[] = {"steam", "-shutdown", NULL};
        char *cmd3[] = {"systemctl", "--user", "stop",
                         "gamescope-session.target", NULL};

        safe_exec(cmd1);
        safe_exec(cmd2);
        safe_exec(cmd3);

        wait_for_user_unit_inactive("gamescope-session.target");
        sleep(SETTLE_DELAY_SEC);  /* colchon para liberacion de VT */

        char *cmd_restart[] = {"sudo", "systemctl", "restart",
                                "plasmalogin.service", NULL};
        safe_exec(cmd_restart);

    } else {
        fprintf(stderr, "Unknown session '%s'\n\n", mode);
        print_usage(argv[0]);
        free(session_id);
        if (user_bus) sd_bus_unref(user_bus);
        return 1;
    }

    free(session_id);
    if (user_bus) sd_bus_unref(user_bus);

    handle_hidden_controllers();
    return 0;
}
