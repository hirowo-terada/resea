#include <message.h>
#include <std/lookup.h>
#include <std/malloc.h>
#include <std/printf.h>
#include <std/syscall.h>
#include <string.h>

static tid_t display_server;

static int cursor_x = 0;
static int cursor_y = 0;
static color_t text_color = COLOR_NORMAL;
static int width;
static int height;
static bool in_esc = false;
static int color_code = 0;

static void newline(void) {
    cursor_x = 0;
    if (cursor_y < height - 1) {
        cursor_y++;
    } else {
        struct message m;
        m.type = SCREEN_SCROLL_MSG;
        ipc_send(display_server, &m);
    }
}

static void update_cursor(void) {
    struct message m;
    m.type = SCREEN_MOVE_CURSOR_MSG;
    m.screen_device.move_cursor.y = cursor_y;
    m.screen_device.move_cursor.x = cursor_x;
    ipc_send(display_server, &m);
}

static void clear_screen(void) {
    struct message m;
    m.type = SCREEN_CLEAR_MSG;
    ipc_send(display_server, &m);
}

void logputc(char ch) {
    if (ch == '\e') {
        in_esc = true;
        color_code = 0;
        return;
    }

    // Handle escape sequences like "\e[1;33m".
    if (in_esc) {
        if ('0' <= ch && ch <= '9') {
            if (color_code > 100) {
                // Invalid (or corrupted) escape sequence. Ignore it to prevent
                // a panic by UBSan.
            } else {
                color_code = (color_code * 10) + (ch - '0');
            }
        } else if (ch == ';') {
            color_code = 0;
        } else if (ch == 'm') {
            in_esc = false;
            switch (color_code) {
                case 32:
                    text_color = COLOR_GREEN;
                    break;
                case 33:
                    text_color = COLOR_YELLOW;
                    break;
                case 91:
                    text_color = COLOR_RED;
                    break;
                case 96:
                    text_color = COLOR_CYAN;
                    break;
                default:
                    text_color = COLOR_NORMAL;
            }
        }

        return;
    }

    if (ch == '\n') {
        newline();
    }

    if (ch != '\n') {
        struct message m;
        m.type = SCREEN_DRAW_CHAR_MSG;
        m.screen_device.draw_char.ch = ch;
        m.screen_device.draw_char.x = cursor_x;
        m.screen_device.draw_char.y = cursor_y;
        m.screen_device.draw_char.fg_color = text_color;
        m.screen_device.draw_char.bg_color = COLOR_BLACK;
        ipc_send(display_server, &m);

        cursor_x++;
        if (cursor_x == width) {
            newline();
        }
    }

    update_cursor();
}

static void poll_kernel_log(void) {
    while (true) {
        char buf[512];
        size_t read_len = klog_read(buf, sizeof(buf));
        if (!read_len) {
            break;
        }

        for (size_t i = 0; i < read_len; i++) {
            logputc(buf[i]);
        }
    }
}

void logputstr(const char *str) {
    while (*str) {
        logputc(*str);
        str++;
    }
}

static void echo_command(int argc, char **argv) {
    for (int i = 1; i < argc; i++) {
        logputstr(argv[i]);
        logputc(' ');
    }

    logputc('\n');
}

static void clear_command(UNUSED int argc, UNUSED char **argv) {
    clear_screen();
    cursor_x = 0;
    cursor_y = 0;
}

struct command {
    const char *name;
    void (*run)(int argc, char **argv);
};

static struct command commands[] = {
    { .name = "echo", .run = echo_command },
    { .name = "clear", .run = clear_command },
    { .name = NULL, .run = NULL },
};

#define CMDLINE_MAX 128

void skip_whitespaces(char **cmdline) {
    while (**cmdline == ' ') {
        (*cmdline)++;
    }
}

int parse(char *cmdline, char **argv, int argv_max) {
    skip_whitespaces(&cmdline);
    if (*cmdline == '\0') {
        return 0;
    }

    int argc = 1;
    argv[0] = cmdline;
    while (*cmdline != '\0') {
        if (*cmdline == ' ') {
            *cmdline++ = '\0';
            skip_whitespaces(&cmdline);
            argv[argc] = cmdline;
            argc++;
            if (argc == argv_max - 1) {
                break;
            }
        } else {
            cmdline++;
        }
    }

    argv[argc] = NULL;
    return argc;
}

void run(const char *cmd_name, int argc, char **argv) {
    for (int i = 0; commands[i].name != NULL; i++) {
        if (!strcmp(commands[i].name, cmd_name)) {
            commands[i].run(argc, argv);
            return;
        }
    }

    logputstr("shell: no such command\n");
}

static char cmdline[CMDLINE_MAX];
static int cursor = 0;

void prompt(void) {
    logputstr("shell> ");
    cursor = 0;
}

static void input(char ch) {
    int argv_max = 8;
    char **argv = malloc(sizeof(char *) * argv_max);
    logputc(ch);
    switch (ch) {
        case '\n': {
            cmdline[cursor] = '\0';
            int argc = parse(cmdline, argv, argv_max);
            if (argc > 0) {
                run(argv[0], argc, argv);
            }
            prompt();
            break;
        }
        default:
            if (cursor == CMDLINE_MAX - 1) {
                logputstr("\nshell: too long input\n");
                prompt();
            } else {
                cmdline[cursor] = ch;
                cursor++;
            }
    }
}

static void get_screen_size(void) {
    struct message m;
    m.type = SCREEN_GET_SIZE_MSG;

    error_t err = ipc_call(display_server, &m);
    ASSERT_OK(err);
    ASSERT(m.type == SCREEN_GET_SIZE_REPLY_MSG);

    height = m.screen_device.display_get_size_reply.height;
    width = m.screen_device.display_get_size_reply.width;
}

void main(void) {
    INFO("starting...");

    display_server = ipc_lookup("display");
    ASSERT_OK(display_server);

    tid_t kbd_server = ipc_lookup("ps2kbd");
    ASSERT_OK(kbd_server);

    get_screen_size();
    clear_screen();
    timer_set(200);
    poll_kernel_log();

    // The mainloop: receive and handle messages.
    prompt();
    while (true) {
        struct message m;
        error_t err = ipc_recv(IPC_ANY, &m);
        ASSERT_OK(err);

        switch (m.type) {
            case NOTIFICATIONS_MSG:
                if (m.notifications.data & NOTIFY_NEW_DATA) {
                    struct message m;
                    m.type = KBD_GET_KEYCODE_MSG;
                    ASSERT_OK(ipc_call(kbd_server, &m));
                    ASSERT(m.type == KBD_KEYCODE_MSG);
                    input(m.shell.key_pressed.keycode);
                }

                if (m.notifications.data & NOTIFY_TIMER) {
                    poll_kernel_log();
                    timer_set(200);
                }
                break;
            default:
                WARN("unknown message type (type=%d)", m.type);
        }
    }
}
