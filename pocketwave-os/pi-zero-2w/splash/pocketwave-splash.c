/* pocketwave framebuffer splash — draws centered text with drop shadow
   Handles both 16bpp (RGB565) and 32bpp (ARGB8888) framebuffers */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <linux/fb.h>

/* 5x7 pixel font */
static const unsigned char font_5x7[][7] = {
    ['p'] = {0x00,0x1E,0x12,0x1E,0x10,0x10,0x10},
    ['o'] = {0x00,0x0E,0x11,0x11,0x11,0x0E,0x00},
    ['c'] = {0x00,0x0E,0x10,0x10,0x10,0x0E,0x00},
    ['k'] = {0x10,0x12,0x14,0x18,0x14,0x12,0x00},
    ['e'] = {0x00,0x0E,0x11,0x1F,0x10,0x0E,0x00},
    ['t'] = {0x04,0x0E,0x04,0x04,0x04,0x06,0x00},
    ['w'] = {0x00,0x11,0x11,0x15,0x15,0x0A,0x00},
    ['a'] = {0x00,0x0E,0x01,0x0F,0x11,0x0F,0x00},
    ['v'] = {0x00,0x11,0x11,0x0A,0x0A,0x04,0x00},
};

static int g_bpp;
static int g_stride;

static inline void put_pixel(uint8_t *fb, int x, int y,
                             uint8_t r, uint8_t g, uint8_t b) {
    if (g_bpp == 16) {
        uint16_t *p = (uint16_t *)(fb + y * g_stride + x * 2);
        *p = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3);
    } else {
        uint32_t *p = (uint32_t *)(fb + y * g_stride + x * 4);
        *p = 0xFF000000 | (r << 16) | (g << 8) | b;
    }
}

static void draw_char(uint8_t *fb, int fb_w, int fb_h,
                      int cx, int cy, char ch,
                      uint8_t r, uint8_t g, uint8_t b, int scale) {
    if ((unsigned char)ch > 127) return;
    const unsigned char *glyph = font_5x7[(unsigned char)ch];
    for (int row = 0; row < 7; row++) {
        for (int col = 0; col < 5; col++) {
            if (glyph[row] & (1 << (4 - col))) {
                for (int sy = 0; sy < scale; sy++) {
                    for (int sx = 0; sx < scale; sx++) {
                        int px = cx + col * scale + sx;
                        int py = cy + row * scale + sy;
                        if (px >= 0 && px < fb_w && py >= 0 && py < fb_h)
                            put_pixel(fb, px, py, r, g, b);
                    }
                }
            }
        }
    }
}

static void draw_text(uint8_t *fb, int fb_w, int fb_h,
                      const char *text, int x, int y,
                      uint8_t r, uint8_t g, uint8_t b, int scale) {
    int spacing = 6 * scale;
    for (int i = 0; text[i]; i++)
        draw_char(fb, fb_w, fb_h, x + i * spacing, y, text[i], r, g, b, scale);
}

static int text_width(const char *text, int scale) {
    return strlen(text) * 6 * scale - scale;
}

int main(int argc, char **argv) {
    int hold = 0;
    for (int i = 1; i < argc; i++)
        if (strcmp(argv[i], "--hold") == 0) hold = 1;

    int fd = open("/dev/fb0", O_RDWR);
    if (fd < 0) { perror("fb0"); return 1; }

    struct fb_var_screeninfo vinfo;
    struct fb_fix_screeninfo finfo;
    ioctl(fd, FBIOGET_VSCREENINFO, &vinfo);
    ioctl(fd, FBIOGET_FSCREENINFO, &finfo);

    int w = vinfo.xres;
    int h = vinfo.yres;
    g_bpp = vinfo.bits_per_pixel;
    g_stride = finfo.line_length;
    size_t fb_size = finfo.smem_len;

    uint8_t *fb = mmap(NULL, fb_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (fb == MAP_FAILED) { perror("mmap"); close(fd); return 1; }

    memset(fb, 0, fb_size);

    const char *text = "pocketwave";
    int scale = w / 80;
    if (scale < 2) scale = 2;

    int tw = text_width(text, scale);
    int th = 7 * scale;
    int x = (w - tw) / 2;
    int y = (h - th) / 2;
    int shd = scale;

    /* Magenta shadow */
    draw_text(fb, w, h, text, x + shd, y + shd, 0xCC, 0x00, 0xCC, scale);
    /* Cyan text */
    draw_text(fb, w, h, text, x, y, 0x00, 0xCC, 0xCC, scale);

    if (hold)
        pause();
    else
        sleep(2);

    munmap(fb, fb_size);
    close(fd);
    return 0;
}
