/* rede: network status window (opened by the taskbar icon). Updates once a second. */

#include <gui.h>

#define W 360
#define H 250

static void row(struct gwin *g, int y, const char *label, const char *value) {
    gui_text(g, 16, y, label, RGB(150, 160, 185), 1);
    gui_text(g, 130, y, value, RGB(225, 232, 245), 1);
}

static void ipstr(char *out, size_t n, uint32_t ip) {
    snprintf(out, n, "%u.%u.%u.%u", ip >> 24, (ip >> 16) & 255, (ip >> 8) & 255, ip & 255);
}

static void render(struct gwin *g) {
    struct sys_netinfo ni;
    netinfo(&ni);
    gui_fill(g, 0, 0, W, H, RGB(26, 30, 46));

    const char *state;
    uint32_t color;
    if (!ni.present) { state = "Sem placa de rede"; color = RGB(130, 134, 150); }
    else if (!ni.link) { state = "Cabo desconectado"; color = RGB(220, 70, 70); }
    else if (!ni.configured) { state = "Aguardando endereco (DHCP)..."; color = RGB(230, 190, 70); }
    else { state = "Conectado"; color = RGB(90, 210, 120); }
    gui_fill(g, 16, 16, 14, 14, color);
    gui_text(g, 40, 14, state, color, 1);

    char b[48];
    int y = 52;
    if (ni.present) {
        row(g, y, "Placa", ni.nic);                                        y += 26;
        snprintf(b, sizeof(b), "%02x:%02x:%02x:%02x:%02x:%02x", ni.mac[0], ni.mac[1], ni.mac[2], ni.mac[3], ni.mac[4], ni.mac[5]);
        row(g, y, "Endereco MAC", b);                                      y += 26;
        if (ni.configured) {
            ipstr(b, sizeof(b), ni.ip);   row(g, y, "Endereco IP", b);     y += 26;
            ipstr(b, sizeof(b), ni.mask); row(g, y, "Mascara", b);         y += 26;
            ipstr(b, sizeof(b), ni.gw);   row(g, y, "Gateway", b);         y += 26;
            ipstr(b, sizeof(b), ni.dns);  row(g, y, "DNS", b);             y += 26;
        }
    } else {
        gui_text(g, 16, y, "Nenhuma placa suportada foi encontrada.", RGB(190, 198, 215), 1);
        gui_text(g, 16, y + 22, "Suportadas: e1000, rtl8139, rtl8169, virtio-net.", RGB(150, 160, 185), 1);
    }
    gui_present(g);
}

int main(void) {
    struct gwin g;
    if (gui_open(&g, W, H, "Rede") < 0) return 1;
    render(&g);
    unsigned long last = uptime_ms();
    struct win_event ev;
    for (;;) {
        int r;
        while ((r = gui_poll(&g, &ev)) > 0) {
            if (ev.type == WIN_EV_CLOSE) return 0;
        }
        if (r < 0) return 0;
        if (uptime_ms() - last >= 1000) { last = uptime_ms(); render(&g); }
        else sleep_ms(50);
    }
}
