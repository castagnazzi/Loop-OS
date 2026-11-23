/* PS/2 Keyboard Driver Implementation */

#include <kernel/drivers/keyboard.h>
#include <kernel/arch/x86_64/idt.h>
#include <kernel/stdio.h>
#include <kernel/string.h>
#include <stdint.h>

/* Keyboard controller ports */
#define KBD_DATA_PORT    0x60
#define KBD_STATUS_PORT  0x64
#define KBD_CMD_PORT     0x64

/* Keyboard commands */
#define KBD_CMD_SET_LEDS     0xED
#define KBD_CMD_ECHO         0xEE
#define KBD_CMD_SCAN_CODE    0xF0
#define KBD_CMD_IDENTIFY     0xF2
#define KBD_CMD_ENABLE       0xF4
#define KBD_CMD_DISABLE      0xF5
#define KBD_CMD_RESET        0xFF

/* Controller commands */
#define KBC_CMD_READ_CONFIG  0x20
#define KBC_CMD_WRITE_CONFIG 0x60
#define KBC_CMD_DISABLE_PORT1 0xAD
#define KBC_CMD_ENABLE_PORT1 0xAE
#define KBC_CMD_TEST_PORT1   0xAB
#define KBC_CMD_TEST_CTRL    0xAA

/* Keyboard buffer */
#define KBD_BUFFER_SIZE 256
static uint8_t kbd_buffer[KBD_BUFFER_SIZE];
static size_t kbd_head = 0, kbd_tail = 0;

/* Keyboard state */
struct keyboard_state kbd_state = {0};

/* Scancode set 1 (default) - key down */
static const char scancode_to_ascii[128] = {
    0, 27, '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', '\b', '\t',
    'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\n', 0, 'a', 's',
    'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`', 0, '\\', 'z', 'x', 'c', 'v',
    'b', 'n', 'm', ',', '.', '/', 0, '*', 0, ' ', 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, '7', '8', '9', '-', '4', '5', '6', '+', '1',
    '2', '3', '0', '.', 0, 0, 0, 0
};

/* Scancode set 1 - shift */
static const char scancode_to_ascii_shift[128] = {
    0, 27, '!', '@', '#', '$', '%', '^', '&', '*', '(', ')', '_', '+', '\b', '\t',
    'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P', '{', '}', '\n', 0, 'A', 'S',
    'D', 'F', 'G', 'H', 'J', 'K', 'L', ':', '"', '~', 0, '|', 'Z', 'X', 'C', 'V',
    'B', 'N', 'M', '<', '>', '?', 0, '*', 0, ' ', 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, '7', '8', '9', '-', '4', '5', '6', '+', '1',
    '2', '3', '0', '.', 0, 0, 0, 0
};

/* ---- ABNT2 (Brazilian) layout ---------------------------------------
 * Entries with the DEAD bit are dead keys: the accent waits for the next letter. Output is Latin-1. */
#define DEAD 0x100
static const uint16_t abnt2_norm[128] = {
    0, 27, '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', '\b', '\t',
    'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', DEAD | 0xB4, '[', '\n', 0, 'a', 's',
    'd', 'f', 'g', 'h', 'j', 'k', 'l', 0xE7, DEAD | '~', '\'', 0, ']', 'z', 'x', 'c', 'v',
    'b', 'n', 'm', ',', '.', ';', 0, '*', 0, ' ', 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, '7', '8', '9', '-', '4', '5', '6', '+', '1',
    '2', '3', '0', '.', 0, 0, '\\', 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, '/', 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};
static const uint16_t abnt2_shift[128] = {
    0, 27, '!', '@', '#', '$', '%', DEAD | 0xA8, '&', '*', '(', ')', '_', '+', '\b', '\t',
    'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P', DEAD | '`', '{', '\n', 0, 'A', 'S',
    'D', 'F', 'G', 'H', 'J', 'K', 'L', 0xC7, DEAD | '^', '"', 0, '}', 'Z', 'X', 'C', 'V',
    'B', 'N', 'M', '<', '>', ':', 0, '*', 0, ' ', 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, '7', '8', '9', '-', '4', '5', '6', '+', '1',
    '2', '3', '0', '.', 0, 0, '|', 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, '?', 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};

/* AltGr: a few symbols */
static uint16_t abnt2_altgr(uint8_t code) {
    switch (code) {
    case 0x10: return '/';   case 0x11: return '?';
    case 0x02: return 0xB9;  case 0x03: return 0xB2;  case 0x04: return 0xB3;
    case 0x05: return 0xA3;  case 0x06: return 0xAC;  case 0x2E: return 0xA2;
    case 0x0D: return 0xA7;  case 0x1B: return 0xAA;  case 0x2B: return 0xBA;
    }
    return 0;
}

static int kbd_layout = 1;          /* 0 = US, 1 = ABNT2 */
static int altgr = 0;
static uint8_t dead = 0;            /* pending accent character, 0 if none */

void keyboard_set_layout(int layout) { kbd_layout = layout ? 1 : 0; dead = 0; }
int keyboard_get_layout(void) { return kbd_layout; }

/* Accent + letter -> Latin-1 letter, or 0 when they do not combine */
static uint8_t compose_accent(uint8_t accent, uint8_t c) {
    static const char vowels[] = "aeiouAEIOU";
    static const uint8_t acute[] = { 0xE1, 0xE9, 0xED, 0xF3, 0xFA, 0xC1, 0xC9, 0xCD, 0xD3, 0xDA };
    static const uint8_t grave[] = { 0xE0, 0xE8, 0xEC, 0xF2, 0xF9, 0xC0, 0xC8, 0xCC, 0xD2, 0xD9 };
    static const uint8_t circ[]  = { 0xE2, 0xEA, 0xEE, 0xF4, 0xFB, 0xC2, 0xCA, 0xCE, 0xD4, 0xDB };
    static const uint8_t diae[]  = { 0xE4, 0xEB, 0xEF, 0xF6, 0xFC, 0xC4, 0xCB, 0xCF, 0xD6, 0xDC };
    const uint8_t *tab = 0;
    switch (accent) {
    case 0xB4: tab = acute; break;
    case '`':  tab = grave; break;
    case '^':  tab = circ; break;
    case 0xA8: tab = diae; break;
    case '~':
        if (c == 'a') return 0xE3;
        if (c == 'o') return 0xF5;
        if (c == 'n') return 0xF1;
        if (c == 'A') return 0xC3;
        if (c == 'O') return 0xD5;
        if (c == 'N') return 0xD1;
        return 0;
    }
    if (!tab) return 0;
    if (accent == 0xB4 && c == 'c') return 0xE7;       /* acute + c = c-cedilla, as on the real layout */
    if (accent == 0xB4 && c == 'C') return 0xC7;
    for (int i = 0; vowels[i]; i++) if (vowels[i] == (char)c) return tab[i];
    return 0;
}

/* Extended scancodes (E0 prefix) */
static const uint8_t extended_scancodes[] = {
    0x1C,  /* Enter (keypad) */
    0x1D,  /* Right Ctrl */
    0x35,  /* Keypad / */
    0x37,  /* Print Screen */
    0x38,  /* Right Alt */
    0x46,  /* Break (Pause) */
    0x47,  /* Home */
    0x48,  /* Up */
    0x49,  /* Page Up */
    0x4B,  /* Left */
    0x4D,  /* Right */
    0x4F,  /* End */
    0x50,  /* Down */
    0x51,  /* Page Down */
    0x52,  /* Insert */
    0x53,  /* Delete */
};

/* I/O helpers */
static inline uint8_t inb(uint16_t port) {
    uint8_t ret;
    asm volatile("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

static inline void outb(uint16_t port, uint8_t val) {
    asm volatile("outb %0, %1" : : "a"(val), "Nd"(port));
}

static inline void io_wait(void) {
    asm volatile("outb %%al, $0x80" : : "a"(0));
}

/* Wait for keyboard controller input buffer empty */
static void kbc_wait_input(void) {
    while (inb(KBD_STATUS_PORT) & 0x02) io_wait();
}

/* Wait for keyboard controller output buffer full */
static void kbc_wait_output(void) {
    while (!(inb(KBD_STATUS_PORT) & 0x01)) io_wait();
}

/* Send command to keyboard */
static void kbd_send_cmd(uint8_t cmd) {
    kbc_wait_input();
    outb(KBD_DATA_PORT, cmd);
}

/* Send command to keyboard controller */
static void kbc_send_cmd(uint8_t cmd) {
    kbc_wait_input();
    outb(KBD_CMD_PORT, cmd);
}

/* Read from keyboard controller */
static uint8_t kbc_read_data(void) {
    kbc_wait_output();
    return inb(KBD_DATA_PORT);
}

/* Initialize keyboard */
void keyboard_init(void) {
    kprintf("Keyboard: initializing...\n");
    
    /* Disable keyboard port */
    kbc_send_cmd(KBC_CMD_DISABLE_PORT1);
    
    /* Flush output buffer */
    while (inb(KBD_STATUS_PORT) & 0x01) {
        inb(KBD_DATA_PORT);
    }
    
    /* Get controller config */
    kbc_send_cmd(KBC_CMD_READ_CONFIG);
    uint8_t config = kbc_read_data();
    
    /* Enable interrupts, disable translation */
    config |= 0x01;   /* IRQ1 enable */
    config &= ~0x40;  /* No translation */
    config &= ~0x10;  /* Enable port 1 */
    
    /* Write config back */
    kbc_send_cmd(KBC_CMD_WRITE_CONFIG);
    kbc_wait_input();
    outb(KBD_DATA_PORT, config);
    
    /* Test keyboard controller */
    kbc_send_cmd(KBC_CMD_TEST_CTRL);
    if (kbc_read_data() != 0x55) {
        kprintf("Keyboard: controller self-test failed\n");
    }
    
    /* Test keyboard port */
    kbc_send_cmd(KBC_CMD_TEST_PORT1);
    uint8_t test_result = kbc_read_data();
    if (test_result != 0x00) {
        kprintf("Keyboard: port test failed (0x%02x)\n", test_result);
    }
    
    /* Enable keyboard port */
    kbc_send_cmd(KBC_CMD_ENABLE_PORT1);
    
    /* Reset keyboard */
    kbd_send_cmd(KBD_CMD_RESET);
    kbc_wait_output();
    uint8_t ack = inb(KBD_DATA_PORT);
    if (ack != 0xFA) {
        kprintf("Keyboard: reset ack failed (0x%02x)\n", ack);
    }
    kbc_wait_output();
    uint8_t bat = inb(KBD_DATA_PORT);
    if (bat != 0xAA) {
        kprintf("Keyboard: BAT failed (0x%02x)\n", bat);
    }
    
    /* Set scancode set 1 */
    kbd_send_cmd(KBD_CMD_SCAN_CODE);
    kbc_wait_output();
    ack = inb(KBD_DATA_PORT);
    kbd_send_cmd(0x01);  /* Set 1 */
    kbc_wait_output();
    ack = inb(KBD_DATA_PORT);
    
    /* Enable keyboard */
    kbd_send_cmd(KBD_CMD_ENABLE);
    kbc_wait_output();
    ack = inb(KBD_DATA_PORT);
    
    /* Clear buffer */
    kbd_head = kbd_tail = 0;
    memset(&kbd_state, 0, sizeof(kbd_state));
    
    /* Route IRQ1 to the keyboard handler and enable it on the PIC */
    irq_register_handler(1, (irq_handler_t)keyboard_handler);
    pic_unmask(1);

    kprintf("Keyboard: initialized\n");
}

/* Navigation keys reach programs as these control codes (they have no ASCII value) */
#define KEY_UP    0x11
#define KEY_DOWN  0x12
#define KEY_LEFT  0x13
#define KEY_RIGHT 0x14
#define KEY_PGUP  0x15
#define KEY_PGDN  0x16

static void kbd_push(uint8_t c) {
    size_t next = (kbd_head + 1) % KBD_BUFFER_SIZE;
    if (next != kbd_tail) {
        kbd_buffer[kbd_head] = c;
        kbd_head = next;
    }
}

/* Process scancode */
static void process_scancode(uint8_t scancode) {
    static int extended = 0;
    
    if (scancode == 0xE0) {
        extended = 1;
        return;
    }
    
    int released = scancode & 0x80;
    uint8_t code = scancode & 0x7F;
    
    /* Handle modifier keys */
    switch (code) {
        case 0x2A: case 0x36:  /* Left/Right Shift */
            kbd_state.shift = !released;
            break;
        case 0x1D:  /* Left Ctrl */
            if (!extended) kbd_state.ctrl = !released;
            break;
        case 0x38:  /* Left Alt */
            if (!extended) kbd_state.alt = !released;
            break;
        case 0x3A:  /* Caps Lock */
            if (!released) kbd_state.caps ^= 1;
            break;
        case 0x45:  /* Num Lock */
            if (!released) kbd_state.num ^= 1;
            break;
        case 0x46:  /* Scroll Lock */
            if (!released) kbd_state.scroll ^= 1;
            break;
    }
    
    /* Handle extended keys */
    if (extended) {
        extended = 0;
        switch (code) {
            case 0x1D: kbd_state.ctrl = !released; break;      /* Right Ctrl */
            case 0x38: altgr = !released; break;               /* Right Alt = AltGr */
            case 0x35: if (!released) kbd_push('/'); break;    /* keypad / */
            case 0x47: /* Home */ break;
            case 0x48: if (!released) kbd_push(KEY_UP); break;
            case 0x49: if (!released) kbd_push(KEY_PGUP); break;
            case 0x4B: if (!released) kbd_push(KEY_LEFT); break;
            case 0x4D: if (!released) kbd_push(KEY_RIGHT); break;
            case 0x4F: /* End */ break;
            case 0x50: if (!released) kbd_push(KEY_DOWN); break;
            case 0x51: if (!released) kbd_push(KEY_PGDN); break;
            case 0x52: /* Insert */ break;
            case 0x53: /* Delete */ break;
        }
        return;
    }
    
    /* ABNT2: tables, AltGr and dead keys */
    if (!released && code < 128 && kbd_layout == 1) {
        uint16_t e;
        if (altgr) e = abnt2_altgr(code);
        else e = kbd_state.shift ? abnt2_shift[code] : abnt2_norm[code];
        if (!altgr && !kbd_state.shift && kbd_state.caps && ((e >= 'a' && e <= 'z') || e == 0xE7)) e -= 32;
        else if (!altgr && kbd_state.shift && kbd_state.caps && ((e >= 'A' && e <= 'Z') || e == 0xC7)) e += 32;
        if (!e) return;
        if (e & DEAD) {
            uint8_t acc = (uint8_t)e;
            if (dead == acc) { kbd_push(acc); dead = 0; }       /* the same accent twice types it */
            else { if (dead) kbd_push(dead); dead = acc; }
            return;
        }
        uint8_t c = (uint8_t)e;
        if (dead) {
            uint8_t acc = dead;
            dead = 0;
            if (c == ' ') { kbd_push(acc); return; }            /* accent + space = the accent */
            uint8_t m = compose_accent(acc, c);
            if (m) { kbd_push(m); return; }
            kbd_push(acc);                                      /* no combination: both characters */
        }
        kbd_push(c);
        return;
    }

    /* Convert to ASCII on key press */
    if (!released && code < 128) {
        char c = 0;
        if (kbd_state.shift) c = scancode_to_ascii_shift[code];
        else c = scancode_to_ascii[code];
        
        /* Handle caps lock for letters */
        if (c >= 'a' && c <= 'z' && kbd_state.caps) c -= 32;
        if (c >= 'A' && c <= 'Z' && kbd_state.caps && kbd_state.shift) c += 32;
        
        if (c) {
            size_t next = (kbd_head + 1) % KBD_BUFFER_SIZE;
            if (next != kbd_tail) {
                kbd_buffer[kbd_head] = c;
                kbd_head = next;
            }
        }
    }
}

/* Keyboard interrupt handler */
void keyboard_handler(void *frame) {
    (void)frame;
    uint8_t scancode = inb(KBD_DATA_PORT);
    process_scancode(scancode);
    /* EOI is sent by irq_common_handler */
}

/* Get character from buffer (non-blocking) */
int keyboard_getchar(void) {
    if (kbd_head == kbd_tail) return -1;
    int c = kbd_buffer[kbd_tail];
    kbd_tail = (kbd_tail + 1) % KBD_BUFFER_SIZE;
    return c;
}

/* Check if key available */
int keyboard_has_key(void) {
    return kbd_head != kbd_tail;
}