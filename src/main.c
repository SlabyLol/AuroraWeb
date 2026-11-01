// AuroraWeb – simple HTTP text browser for AuroraOS
// English UI. Only plain HTTP (no TLS yet).
#include <aurora_app.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#define MAX_URL     256
#define MAX_TITLE   128
#define MAX_BODY    (180 * 1024)   /* keep under the ~1 MB HTTP limit */
#define HISTORY_MAX 16

static char current_url[MAX_URL] = "http://example.com/";
static char page_title[MAX_TITLE] = "AuroraWeb";
static char *page_body = NULL;
static size_t page_len = 0;
static int scroll_y = 0;
static int loading = 0;
static char status[128] = "Ready";
static int spinner = 0;

/* simple history */
static char history[HISTORY_MAX][MAX_URL];
static int hist_count = 0;
static int hist_pos = -1;

static void set_status(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(status, sizeof(status), fmt, ap);
    va_end(ap);
}

static void push_history(const char *url) {
    if (hist_pos >= 0 && strcmp(history[hist_pos], url) == 0)
        return;
    if (hist_pos < HISTORY_MAX - 1)
        hist_pos++;
    else {
        memmove(history[0], history[1], (HISTORY_MAX - 1) * MAX_URL);
    }
    strncpy(history[hist_pos], url, MAX_URL - 1);
    history[hist_pos][MAX_URL - 1] = 0;
    hist_count = hist_pos + 1;
}

/* crude title extraction */
static void extract_title(const char *html, size_t len) {
    const char *p = html;
    const char *end = html + len;
    while (p + 7 < end) {
        if ((p[0] == '<' || p[0] == '<') &&
            (p[1] == 't' || p[1] == 'T') &&
            (p[2] == 'i' || p[2] == 'I') &&
            (p[3] == 't' || p[3] == 'T') &&
            (p[4] == 'l' || p[4] == 'L') &&
            (p[5] == 'e' || p[5] == 'E') &&
            p[6] == '>') {
            p += 7;
            size_t i = 0;
            while (p < end && *p != '<' && i < MAX_TITLE - 1) {
                if (*p != '\r' && *p != '\n')
                    page_title[i++] = *p;
                p++;
            }
            page_title[i] = 0;
            return;
        }
        p++;
    }
    strncpy(page_title, "(no title)", MAX_TITLE - 1);
}

/* very simple HTML → plain text (strip tags) */
static void html_to_text(const char *html, size_t len, char *out, size_t out_max) {
    size_t oi = 0;
    int in_tag = 0;
    int in_script = 0;
    int in_style = 0;

    for (size_t i = 0; i < len && oi + 1 < out_max; i++) {
        char c = html[i];

        if (c == '<') {
            in_tag = 1;
            /* detect script / style */
            if (i + 7 < len) {
                if ((html[i+1]=='s'||html[i+1]=='S') &&
                    (html[i+2]=='c'||html[i+2]=='C') &&
                    (html[i+3]=='r'||html[i+3]=='R'))
                    in_script = 1;
                if ((html[i+1]=='s'||html[i+1]=='S') &&
                    (html[i+2]=='t'||html[i+2]=='T') &&
                    (html[i+3]=='y'||html[i+3]=='Y'))
                    in_style = 1;
            }
            if (i + 8 < len && html[i+1]=='/' ) {
                if ((html[i+2]=='s'||html[i+2]=='S') &&
                    (html[i+3]=='c'||html[i+3]=='C'))
                    in_script = 0;
                if ((html[i+2]=='s'||html[i+2]=='S') &&
                    (html[i+3]=='t'||html[i+3]=='T'))
                    in_style = 0;
            }
            continue;
        }
        if (c == '>') {
            in_tag = 0;
            continue;
        }
        if (in_tag || in_script || in_style)
            continue;

        /* collapse whitespace a bit */
        if (c == '\r') continue;
        if (c == '\n' || c == '\t') c = ' ';
        if (c == ' ' && oi > 0 && out[oi-1] == ' ') continue;

        out[oi++] = c;
    }
    out[oi] = 0;
}

static void on_net_wait(void) {
    spinner = (spinner + 1) % 4;
    const char *spin = "|/-\\";
    gfx_clear(SCREEN_BOTTOM, RGB(15, 20, 30));
    gfx_print(SCREEN_BOTTOM, 20, 90, FONT_REGULAR, COLOR_AURORA,
              "Loading... %c", spin[spinner]);
    gfx_print(SCREEN_BOTTOM, 20, 120, FONT_SMALL, COLOR_GRAY, "%s", current_url);
    gfx_present();
}

static void load_url(const char *url) {
    if (!url || !url[0]) return;

    /* force http */
    char final[MAX_URL];
    if (strncmp(url, "https://", 8) == 0) {
        snprintf(final, sizeof(final), "http://%s", url + 8);
        set_status("HTTPS not supported – using HTTP");
    } else if (strncmp(url, "http://", 7) != 0) {
        snprintf(final, sizeof(final), "http://%s", url);
    } else {
        strncpy(final, url, MAX_URL - 1);
        final[MAX_URL - 1] = 0;
    }

    strncpy(current_url, final, MAX_URL - 1);
    current_url[MAX_URL - 1] = 0;
    loading = 1;
    scroll_y = 0;
    set_status("Connecting...");

    net_on_wait(on_net_wait);

    if (!net_connect()) {
        set_status("%s", net_error());
        loading = 0;
        return;
    }

    set_status("Fetching...");
    HttpResponse *r = http_get(final);
    loading = 0;

    if (!r) {
        set_status("%s", net_error());
        return;
    }

    set_status("HTTP %d – %u bytes", r->status, (unsigned)r->length);

    if (page_body) {
        free(page_body);
        page_body = NULL;
    }

    size_t take = r->length;
    if (take > MAX_BODY) take = MAX_BODY;

    page_body = (char *)malloc(take + 1);
    if (!page_body) {
        set_status("Out of memory");
        http_free(r);
        return;
    }

    memcpy(page_body, r->body, take);
    page_body[take] = 0;
    page_len = take;

    extract_title(page_body, page_len);

    /* convert to readable text in-place for display */
    char *text = (char *)malloc(take + 1);
    if (text) {
        html_to_text(page_body, page_len, text, take + 1);
        free(page_body);
        page_body = text;
        page_len = strlen(page_body);
    }

    push_history(final);
    http_free(r);
}

/* crude on-screen keyboard for URL entry */
static int editing = 0;
static char edit_buf[MAX_URL];
static int edit_pos = 0;

static const char *kb_rows[] = {
    "1234567890",
    "qwertyuiop",
    "asdfghjkl",
    "zxcvbnm./:",
    NULL
};

static void draw_keyboard(void) {
    int y = 100;
    for (int r = 0; kb_rows[r]; r++) {
        int x = 10 + (r == 3 ? 20 : 0);
        for (int c = 0; kb_rows[r][c]; c++) {
            char ch[2] = { kb_rows[r][c], 0 };
            gfx_rect(SCREEN_BOTTOM, x, y, 22, 22, RGB(40, 50, 70));
            gfx_print(SCREEN_BOTTOM, x + 6, y + 4, FONT_SMALL, COLOR_WHITE, "%s", ch);
            x += 26;
        }
        y += 28;
    }
    /* special keys */
    gfx_rect(SCREEN_BOTTOM, 10, 212, 70, 24, RGB(60, 40, 40));
    gfx_print(SCREEN_BOTTOM, 18, 216, FONT_SMALL, COLOR_WHITE, "DEL");
    gfx_rect(SCREEN_BOTTOM, 90, 212, 70, 24, RGB(40, 60, 40));
    gfx_print(SCREEN_BOTTOM, 100, 216, FONT_SMALL, COLOR_WHITE, "GO");
    gfx_rect(SCREEN_BOTTOM, 170, 212, 70, 24, RGB(40, 40, 60));
    gfx_print(SCREEN_BOTTOM, 180, 216, FONT_SMALL, COLOR_WHITE, "ESC");
    gfx_rect(SCREEN_BOTTOM, 250, 212, 60, 24, RGB(50, 50, 50));
    gfx_print(SCREEN_BOTTOM, 258, 216, FONT_SMALL, COLOR_WHITE, "SPC");
}

static void handle_keyboard_touch(int tx, int ty) {
    if (ty >= 212) {
        if (tx < 80) { /* DEL */
            if (edit_pos > 0) {
                edit_pos--;
                edit_buf[edit_pos] = 0;
            }
        } else if (tx < 160) { /* GO */
            editing = 0;
            load_url(edit_buf);
        } else if (tx < 240) { /* ESC */
            editing = 0;
        } else { /* SPC */
            if (edit_pos < MAX_URL - 1) {
                edit_buf[edit_pos++] = ' ';
                edit_buf[edit_pos] = 0;
            }
        }
        return;
    }

    int y = 100;
    for (int r = 0; kb_rows[r]; r++) {
        int x = 10 + (r == 3 ? 20 : 0);
        for (int c = 0; kb_rows[r][c]; c++) {
            if (tx >= x && tx < x + 22 && ty >= y && ty < y + 22) {
                if (edit_pos < MAX_URL - 1) {
                    edit_buf[edit_pos++] = kb_rows[r][c];
                    edit_buf[edit_pos] = 0;
                }
                return;
            }
            x += 26;
        }
        y += 28;
    }
}

int main(void) {
    page_body = strdup("Welcome to AuroraWeb\n\n"
                       "A simple HTTP text browser for AuroraOS.\n\n"
                       "Controls:\n"
                       "  A          – Open / Reload URL\n"
                       "  B          – Back in history\n"
                       "  X          – Edit address bar\n"
                       "  Y          – Go to example.com\n"
                       "  D-Pad / CPAD – Scroll\n"
                       "  START      – Quit\n\n"
                       "Note: Only plain HTTP is supported\n"
                       "(no HTTPS / TLS yet).\n");
    page_len = strlen(page_body);

    while (app_loop()) {
        uint32_t down = hid_keys_down();

        if (down & KEY_START)
            break;

        if (editing) {
            if (hid_touch_down()) {
                handle_keyboard_touch(hid_touch_x(), hid_touch_y());
            }
            if (down & KEY_B)
                editing = 0;
        } else {
            if (down & KEY_A) {
                load_url(current_url);
            }
            if (down & KEY_B) {
                if (hist_pos > 0) {
                    hist_pos--;
                    load_url(history[hist_pos]);
                }
            }
            if (down & KEY_X) {
                editing = 1;
                strncpy(edit_buf, current_url, MAX_URL - 1);
                edit_buf[MAX_URL - 1] = 0;
                edit_pos = (int)strlen(edit_buf);
            }
            if (down & KEY_Y) {
                load_url("http://example.com/");
            }

            /* scroll */
            int cpx, cpy;
            hid_cpad(&cpx, &cpy);
            if (down & KEY_UP || cpy > 40)   scroll_y -= 12;
            if (down & KEY_DOWN || cpy < -40) scroll_y += 12;
            if (down & KEY_LEFT)  scroll_y -= 60;
            if (down & KEY_RIGHT) scroll_y += 60;
            if (scroll_y < 0) scroll_y = 0;
        }

        /* ===== TOP SCREEN ===== */
        gfx_clear(SCREEN_TOP, RGB(12, 14, 22));

        /* title bar */
        gfx_rect(SCREEN_TOP, 0, 0, 400, 28, RGB(25, 35, 55));
        gfx_print(SCREEN_TOP, 8, 6, FONT_BOLD, COLOR_AURORA, "AuroraWeb");
        gfx_print(SCREEN_TOP, 140, 8, FONT_SMALL, COLOR_GRAY, "%s", page_title);

        /* URL bar */
        gfx_rect(SCREEN_TOP, 4, 32, 392, 22, RGB(30, 40, 60));
        gfx_print(SCREEN_TOP, 8, 36, FONT_SMALL, COLOR_WHITE, "%s", current_url);

        /* status */
        gfx_print(SCREEN_TOP, 8, 58, FONT_SMALL, COLOR_GRAY, "%s", status);

        /* page content */
        if (page_body) {
            int line = 0;
            const char *p = page_body;
            int y = 78 - scroll_y;

            while (*p && y < 230) {
                char linebuf[64];
                int li = 0;
                while (*p && *p != '\n' && li < 58) {
                    linebuf[li++] = *p++;
                }
                if (*p == '\n') p++;
                linebuf[li] = 0;

                if (y >= 78) {
                    gfx_print(SCREEN_TOP, 8, y, FONT_SMALL, COLOR_WHITE, "%s", linebuf);
                }
                y += 14;
                line++;
            }
        }

        /* ===== BOTTOM SCREEN ===== */
        gfx_clear(SCREEN_BOTTOM, RGB(15, 20, 30));

        if (editing) {
            gfx_print(SCREEN_BOTTOM, 10, 10, FONT_REGULAR, COLOR_AURORA, "Enter URL:");
            gfx_rect(SCREEN_BOTTOM, 8, 36, 304, 24, RGB(30, 40, 60));
            gfx_print(SCREEN_BOTTOM, 12, 40, FONT_SMALL, COLOR_WHITE, "%s", edit_buf);
            /* cursor */
            int cw = gfx_text_width(FONT_SMALL, edit_buf);
            gfx_rect(SCREEN_BOTTOM, 12 + cw, 40, 2, 14, COLOR_AURORA);
            draw_keyboard();
        } else {
            gfx_print(SCREEN_BOTTOM, 12, 16, FONT_REGULAR, COLOR_WHITE, "Controls");
            gfx_print(SCREEN_BOTTOM, 12, 48, FONT_SMALL, COLOR_GRAY, "A  Reload current page");
            gfx_print(SCREEN_BOTTOM, 12, 68, FONT_SMALL, COLOR_GRAY, "B  Back in history");
            gfx_print(SCREEN_BOTTOM, 12, 88, FONT_SMALL, COLOR_GRAY, "X  Edit address bar");
            gfx_print(SCREEN_BOTTOM, 12, 108, FONT_SMALL, COLOR_GRAY, "Y  example.com");
            gfx_print(SCREEN_BOTTOM, 12, 128, FONT_SMALL, COLOR_GRAY, "D-Pad / Circle Pad  Scroll");
            gfx_print(SCREEN_BOTTOM, 12, 148, FONT_SMALL, COLOR_GRAY, "START  Quit");

            gfx_print(SCREEN_BOTTOM, 12, 190, FONT_SMALL, COLOR_AURORA,
                      "HTTP only – no HTTPS yet");
            gfx_print(SCREEN_BOTTOM, 12, 210, FONT_SMALL, COLOR_GRAY,
                      "History: %d / %d", hist_pos + 1, hist_count);
        }
    }

    if (page_body) free(page_body);
    return 0;
}
