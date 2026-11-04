/* art_recipe_test: the art recipe runner (port/src/common/art_recipe.c) on an
 * extracted disc, outside the game: each image of each recipe file written as
 * a PNG under OUT at its asset path. tests/port/art_recipe_check.py runs it
 * beside the authoring tool (art/tools/recipes.py) and compares the pixels.
 * Built per version (the icon ids are the build's): ninja art_recipe_test.
 *
 * usage: art_recipe_test DISC_DIR OUT_DIR FILE.recipe...
 *   DISC_DIR: the extracted disc (holding files/), as tools/port/extract_disc.py makes it */
#include "port/art_recipe.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <direct.h>
#define MKDIR(p) _mkdir(p)
#else
#include <sys/stat.h>
#define MKDIR(p) mkdir(p, 0755)
#endif

static uint8_t* readFile(const char* path, uint32_t* size) {
    FILE* f = fopen(path, "rb");
    uint8_t* data;
    long n;
    if (f == NULL) {
        return NULL;
    }
    fseek(f, 0, SEEK_END);
    n = ftell(f);
    fseek(f, 0, SEEK_SET);
    data = (uint8_t*)malloc((size_t)n + 1);
    if (data != NULL && fread(data, 1, (size_t)n, f) != (size_t)n) {
        free(data);
        data = NULL;
    }
    fclose(f);
    if (data != NULL) {
        data[n] = 0;
        *size = (uint32_t)n;
    }
    return data;
}

static uint8_t* discRead(void* user, const char* path, uint32_t* size) {
    char full[1024];
    snprintf(full, sizeof(full), "%s/files/%s", (const char*)user, path);
    return readFile(full, size);
}

static uint32_t crc(const uint8_t* p, size_t n, uint32_t c) {
    size_t i;
    int k;
    c = ~c;
    for (i = 0; i < n; i++) {
        c ^= p[i];
        for (k = 0; k < 8; k++) {
            c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1)));
        }
    }
    return ~c;
}

static void be(uint8_t* p, uint32_t v) {
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)v;
}

static void chunk(FILE* f, const char* type, const uint8_t* d, uint32_t n) {
    uint8_t h[8];
    uint32_t c = crc((const uint8_t*)type, 4, 0);
    c = crc(d, n, c);
    be(h, n);
    memcpy(h + 4, type, 4);
    fwrite(h, 1, 8, f);
    fwrite(d, 1, n, f);
    be(h, c);
    fwrite(h, 1, 4, f);
}

/* an RGBA PNG, its data in stored (uncompressed) deflate blocks */
static int writePng(const char* path, const PortArtImage* img) {
    const size_t rowLen = (size_t)img->w * 4 + 1, rawLen = rowLen * img->h;
    uint8_t* raw = (uint8_t*)malloc(rawLen);
    uint8_t* z = (uint8_t*)malloc(rawLen + rawLen / 65535 * 5 + 16);
    size_t i, zn = 0;
    uint32_t a = 1, b = 0;
    uint8_t ihdr[13];
    FILE* f;
    int y;
    if (raw == NULL || z == NULL) {
        return 0;
    }
    for (y = 0; y < img->h; y++) {
        raw[y * rowLen] = 0;
        memcpy(raw + y * rowLen + 1, img->px + (size_t)y * img->w * 4, (size_t)img->w * 4);
    }
    z[zn++] = 0x78;
    z[zn++] = 0x01;
    for (i = 0; i < rawLen || i == 0; i += 65535) {
        const size_t n = rawLen - i < 65535 ? rawLen - i : 65535;
        z[zn++] = (uint8_t)(i + n >= rawLen);
        z[zn++] = (uint8_t)n;
        z[zn++] = (uint8_t)(n >> 8);
        z[zn++] = (uint8_t)~n;
        z[zn++] = (uint8_t)(~n >> 8);
        memcpy(z + zn, raw + i, n);
        zn += n;
        if (n == 0) {
            break;
        }
    }
    for (i = 0; i < rawLen; i++) {
        a = (a + raw[i]) % 65521;
        b = (b + a) % 65521;
    }
    be(z + zn, b << 16 | a);
    zn += 4;
    f = fopen(path, "wb");
    if (f == NULL) {
        free(raw);
        free(z);
        return 0;
    }
    fwrite("\x89PNG\r\n\x1a\n", 1, 8, f);
    be(ihdr, (uint32_t)img->w);
    be(ihdr + 4, (uint32_t)img->h);
    ihdr[8] = 8;
    ihdr[9] = 6;
    ihdr[10] = ihdr[11] = ihdr[12] = 0;
    chunk(f, "IHDR", ihdr, 13);
    chunk(f, "IDAT", z, (uint32_t)zn);
    chunk(f, "IEND", NULL, 0);
    fclose(f);
    free(raw);
    free(z);
    return 1;
}

static void makeDirs(char* path) {
    char* p;
    for (p = path + 1; *p; p++) {
        if (*p == '/' || *p == '\\') {
            const char c = *p;
            *p = 0;
            MKDIR(path);
            *p = c;
        }
    }
}

typedef struct Job {
    const char* text;
    const char* out;
    const char* file;
    PortArtDisc* disc;
    int made, failed;
} Job;

static void one(void* user, const char* name) {
    Job* j = (Job*)user;
    PortArtImage img;
    char err[256], path[1024];
    const int made = portArtMake(j->text, name, j->disc, &img, err, sizeof(err));
    if (made == -2) {
        printf("%s: drawn on this disc\n", name);
        return;
    }
    if (made < 0) {
        printf("%s: not made on this disc\n", name);
        return;
    }
    if (!made) {
        printf("%s: %s: %s\n", j->file, name, err);
        j->failed++;
        return;
    }
    snprintf(path, sizeof(path), "%s/%s", j->out, name);
    makeDirs(path);
    if (!writePng(path, &img)) {
        printf("%s: can't write\n", path);
        j->failed++;
    } else {
        printf("%s: %dx%d\n", name, img.w, img.h);
        j->made++;
    }
    free(img.px);
}

int main(int argc, char** argv) {
    Job j;
    int i;
    if (argc < 4) {
        fprintf(stderr, "usage: art_recipe_test DISC_DIR OUT_DIR FILE.recipe...\n");
        return 2;
    }
    memset(&j, 0, sizeof(j));
    j.out = argv[2];
    {
        /* the disc's id: sys/boot.bin's first six bytes */
        char boot[1024], id[8] = "";
        uint32_t n = 0;
        uint8_t* b;
        snprintf(boot, sizeof(boot), "%s/sys/boot.bin", argv[1]);
        b = readFile(boot, &n);
        if (b != NULL && n >= 6) {
            memcpy(id, b, 6);
            id[6] = 0;
        }
        free(b);
        j.disc = portArtDiscOpen(discRead, argv[1], id);
    }
    for (i = 3; i < argc; i++) {
        uint32_t size;
        char* text = (char*)readFile(argv[i], &size);
        if (text == NULL) {
            printf("%s: can't read\n", argv[i]);
            j.failed++;
            continue;
        }
        j.text = text;
        j.file = argv[i];
        portArtImages(text, one, &j);
        free(text);
    }
    portArtDiscClose(j.disc);
    printf("%d made, %d failed\n", j.made, j.failed);
    return j.failed ? 1 : 0;
}
