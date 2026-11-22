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
    
    kprintf("Keyboard: initialized\n");
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
            case 0x38: kbd_state.alt = !released; break;       /* Right Alt */
            case 0x47: /* Home */ break;
            case 0x48: /* Up */ break;
            case 0x49: /* Page Up */ break;
            case 0x4B: /* Left */ break;
            case 0x4D: /* Right */ break;
            case 0x4F: /* End */ break;
            case 0x50: /* Down */ break;
            case 0x51: /* Page Down */ break;
            case 0x52: /* Insert */ break;
            case 0x53: /* Delete */ break;
        }
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
    pic_send_eoi(1);
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