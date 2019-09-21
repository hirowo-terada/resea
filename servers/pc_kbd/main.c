#include <resea.h>
#include <resea_idl.h>
#include <server.h>
#include <driver/io.h>
#include "keymap.h"
#include "pc_kbd.h"

// Channels connected by memmgr.
static cid_t memmgr_ch = 1;
static cid_t kernel_ch = 2;
// The channel at which we receive messages.
static cid_t server_ch;
// The channel to receive interrupt notifications. Transfers to server_ch.
static cid_t kbd_irq_ch;
// The channel to receive discovery.connect_request. Transfers to server_ch.
static cid_t discovery_ch;
// The channel to send keyboard events.
static cid_t listener_ch = 0;

static io_handle_t io_handle;
static struct modifiers_state modifiers;

static void read_keyboard_input(void) {
    uint8_t scan_code = io_read8(&io_handle, IOPORT_OFFSET_DATA);
    bool shift = modifiers.shift_left || modifiers.shift_right;
    uint8_t key_code = scan_code2key_code(scan_code, shift);
    char ascii = '_';
    switch (key_code & ~KEY_RELEASE) {
    case KEY_SHIFT_LEFT:
        modifiers.shift_left = (key_code & KEY_RELEASE) == 0;
        break;
    case KEY_SHIFT_RIGHT:
        modifiers.shift_right = (key_code & KEY_RELEASE) == 0;
        break;
    default:
        ascii = key_code & ~KEY_RELEASE;
    }

    TRACE("keyboard: scan_code=%x, key_code=%x, ascii='%c'%s%s%s%s%s%s%s",
          scan_code, key_code, ascii,
          (key_code & KEY_RELEASE) ? " [release]"     : "",
          modifiers.ctrl_left      ? " [ctrl-left]"   : "",
          modifiers.ctrl_right     ? " [ctrl-right]"  : "",
          modifiers.alt_left       ? " [alt-left]"    : "",
          modifiers.alt_right      ? " [alt-right]"   : "",
          modifiers.shift_left     ? " [shift-left]"  : "",
          modifiers.shift_right    ? " [shift-right]" : ""
         );

    if (listener_ch) {
        call_keyboard_driver_on_keyinput(listener_ch, key_code);
    }
}

static error_t handle_notification(struct message *m) {
    intmax_t intr_count = m->payloads.notification.data;

    WARN("copy_message: %d // %d %d", INLINE_PAYLOAD_LEN(get_ipc_buffer()->header),
        offsetof(struct message, payloads.data),
        offsetof(struct message, payloads.notification.data));

    INFO("intr! %x %d", get_ipc_buffer()->header, intr_count);
    while (intr_count-- > 0) {
        read_keyboard_input();
    }
    return ERR_DONT_REPLY;
}

static error_t handle_server_connect(struct message *m) {
    cid_t new_ch;
    TRY(open(&new_ch));
    transfer(new_ch, server_ch);

    m->header = SERVER_CONNECT_REPLY_HEADER;
    m->payloads.server.connect_reply.ch = new_ch;
    return OK;
}

static error_t handle_listen_keyboard(struct message *m) {
    listener_ch = m->payloads.keyboard_driver.listen_keyboard.ch;

    m->header = KEYBOARD_DRIVER_LISTEN_KEYBOARD_REPLY_HEADER;
    return OK;
}

static error_t process_message(struct message *m) {
    switch (MSG_TYPE(m->header)) {
    case NOTIFICATION_MSG:    return handle_notification(m);
    case SERVER_CONNECT_MSG:  return handle_server_connect(m);
    case KEYBOARD_DRIVER_LISTEN_KEYBOARD_MSG: return handle_listen_keyboard(m);
    }
    return ERR_INVALID_MESSAGE;
}

void main(void) {
    INFO("starting...");

    TRY_OR_PANIC(open(&server_ch));
    TRY_OR_PANIC(io_open(&io_handle, kernel_ch, IO_SPACE_IOPORT,
                         IOPORT_ADDR, IOPORT_SIZE));
    TRY_OR_PANIC(io_listen_interrupt(kernel_ch, server_ch, KEYBOARD_IRQ,
                                     &kbd_irq_ch));
    TRY_OR_PANIC(server_register(memmgr_ch, server_ch,
                                 KEYBOARD_DRIVER_INTERFACE, &discovery_ch));

    // FIXME: memmgr waits for us due to connect_request.
    // INFO("entering the mainloop...");
    server_mainloop(server_ch, process_message);
}