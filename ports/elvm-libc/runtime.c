#include <stddef.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#define BFIO_FILE ((FILE *) 2)

FILE *stdin = (FILE *) 1;
FILE *stdout = (FILE *) 1;
FILE *stderr = (FILE *) 1;

static int wad_pos;
static int wad_size;
static int g_ungot = EOF;
static int eof_seen;

static void print_str(const char *p) {
  for (; *p; p++) {
    putchar(*p);
  }
}

static char *stringify_uint(unsigned int v, char *p, int base) {
  *p = 0;
  do {
    int c = v % base;
    *--p = c < 10 ? c + '0' : c - 10 + 'a';
    v /= base;
  } while (v);
  return p;
}

static char *stringify_int(int v, char *p) {
  unsigned int u;
  int neg = v < 0;
  if (neg) {
    u = (unsigned int) -v;
  } else {
    u = (unsigned int) v;
  }
  p = stringify_uint(u, p, 10);
  if (neg) {
    *--p = '-';
  }
  return p;
}

static int append_char(char *buf, size_t size, size_t *off, int c) {
  if (*off + 1 < size) {
    buf[*off] = c;
  }
  (*off)++;
  return 1;
}

static int append_str(char *buf, size_t size, size_t *off, const char *s) {
  int n = 0;
  if (s == NULL) {
    s = "(null)";
  }
  while (*s) {
    append_char(buf, size, off, *s++);
    n++;
  }
  return n;
}

static int small_strlen(const char *s) {
  int n = 0;
  while (s[n]) {
    n++;
  }
  return n;
}

static void append_repeat(char *buf, size_t size, size_t *off, int c, int n) {
  while (n > 0) {
    append_char(buf, size, off, c);
    n--;
  }
}

static void append_number(char *buf, size_t size, size_t *off, const char *s,
                          int width, int precision, int precision_set,
                          int zero_pad) {
  int neg = *s == '-';
  int digits = small_strlen(s + neg);
  int zeros = 0;
  int total;

  if (precision_set && precision > digits) {
    zeros = precision - digits;
  } else if (!precision_set && zero_pad && width > neg + digits) {
    zeros = width - neg - digits;
  }

  total = neg + zeros + digits;
  if (width > total) {
    append_repeat(buf, size, off, ' ', width - total);
  }
  if (neg) {
    append_char(buf, size, off, '-');
    s++;
  }
  if (zeros > 0) {
    append_repeat(buf, size, off, '0', zeros);
  }
  append_str(buf, size, off, s);
}

static void append_padded_str(char *buf, size_t size, size_t *off,
                              const char *s, int width, int precision,
                              int precision_set) {
  int len;
  int i;

  if (s == NULL) {
    s = "(null)";
  }

  len = small_strlen(s);
  if (precision_set && precision < len) {
    len = precision;
  }

  if (width > len) {
    append_repeat(buf, size, off, ' ', width - len);
  }
  for (i = 0; i < len; ++i) {
    append_char(buf, size, off, s[i]);
  }
}

int vsnprintf(char *buf, size_t size, const char *fmt, va_list ap) {
  size_t off = 0;
  char tmp[32];

  if (size > 0) {
    buf[0] = 0;
  }

  for (; *fmt; fmt++) {
    if (*fmt != '%') {
      append_char(buf, size, &off, *fmt);
      continue;
    }

    fmt++;
    int zero_pad = 0;
    int width = 0;
    int precision = 0;
    int precision_set = 0;

    if (*fmt == '0') {
      zero_pad = 1;
      fmt++;
    }
    while (*fmt >= '0' && *fmt <= '9') {
      width = width * 10 + *fmt++ - '0';
    }
    if (*fmt == '.') {
      fmt++;
      precision_set = 1;
      precision = 0;
      while (*fmt >= '0' && *fmt <= '9') {
        precision = precision * 10 + *fmt++ - '0';
      }
    }
    while (*fmt == 'l' || *fmt == 'z') {
      fmt++;
    }

    switch (*fmt) {
      case 'd':
      case 'i':
        append_number(buf, size, &off,
                      stringify_int(va_arg(ap, int), tmp + sizeof(tmp) - 1),
                      width, precision, precision_set, zero_pad);
        break;
      case 'u':
        append_number(buf, size, &off,
                      stringify_uint(va_arg(ap, unsigned int), tmp + sizeof(tmp) - 1, 10),
                      width, precision, precision_set, zero_pad);
        break;
      case 'x':
      case 'X':
      case 'p':
        append_number(buf, size, &off,
                      stringify_uint(va_arg(ap, unsigned int), tmp + sizeof(tmp) - 1, 16),
                      width, precision, precision_set, zero_pad);
        break;
      case 's':
        append_padded_str(buf, size, &off, va_arg(ap, char *), width, precision,
                          precision_set);
        break;
      case 'c':
        if (width > 1) {
          append_repeat(buf, size, &off, ' ', width - 1);
        }
        append_char(buf, size, &off, va_arg(ap, int));
        break;
      case '%':
        append_char(buf, size, &off, '%');
        break;
      default:
        append_char(buf, size, &off, '%');
        append_char(buf, size, &off, *fmt);
        break;
    }
  }

  if (size > 0) {
    if (off >= size) {
      buf[size - 1] = 0;
    } else {
      buf[off] = 0;
    }
  }

  return off;
}

int vsprintf(char *buf, const char *fmt, va_list ap) {
  return vsnprintf(buf, 256, fmt, ap);
}

int snprintf(char *buf, size_t size, const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  int r = vsnprintf(buf, size, fmt, ap);
  va_end(ap);
  return r;
}

int sprintf(char *buf, const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  int r = vsprintf(buf, fmt, ap);
  va_end(ap);
  return r;
}

int vprintf(const char *fmt, va_list ap) {
  char buf[256];
  int r = vsnprintf(buf, sizeof(buf), fmt, ap);
  print_str(buf);
  return r;
}

int printf(const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  int r = vprintf(fmt, ap);
  va_end(ap);
  return r;
}

int fprintf(FILE *fp, const char *fmt, ...) {
  va_list ap;
  (void) fp;
  va_start(ap, fmt);
  int r = vprintf(fmt, ap);
  va_end(ap);
  return r;
}

int vfprintf(FILE *fp, const char *fmt, va_list ap) {
  (void) fp;
  return vprintf(fmt, ap);
}

int puts(const char *p) {
  print_str(p);
  putchar('\n');
  return 0;
}

int fileno(FILE *fp) {
  return fp == BFIO_FILE ? 2 : 0;
}

static void bfio_prefix(int cmd) {
  putchar(255);
  putchar(0);
  putchar('B');
  putchar('F');
  putchar('I');
  putchar('O');
  putchar(cmd);
}

static void bfio_send_u24(int value) {
  putchar(value & 255);
  putchar((value >> 8) & 255);
  putchar((value >> 16) & 255);
}

static int bfio_recv_nibble(void) {
  int c = getchar();
  c &= 255;
  if (c == 255) {
    return 0;
  }
  return c - 1;
}

static int bfio_recv_byte(void) {
  int hi = bfio_recv_nibble();
  int lo = bfio_recv_nibble();
  return ((hi & 15) << 4) | (lo & 15);
}

static int bfio_recv_u24(void) {
  int a = bfio_recv_byte();
  int b = bfio_recv_byte();
  int c = bfio_recv_byte();
  return a | (b << 8) | (c << 16);
}

static void bfio_send_cstr(const char *s) {
  while (*s) {
    putchar(*s++);
  }
  putchar(0);
}

static int bfio_path_size(const char *path) {
  bfio_prefix('S');
  bfio_send_cstr(path);
  return bfio_recv_u24();
}

static int is_read_mode(const char *mode) {
  return mode != NULL && mode[0] == 'r';
}

FILE *fopen(const char *filename, const char *mode) {
  if (!is_read_mode(mode)) {
    return NULL;
  }

  int size = bfio_path_size(filename);
  if (size <= 0) {
    return NULL;
  }

  wad_size = size;
  wad_pos = 0;
  return BFIO_FILE;
}

int fclose(FILE *fp) {
  (void) fp;
  return 0;
}

int fflush(FILE *fp) {
  (void) fp;
  return 0;
}

int ftell(FILE *fp) {
  if (fp == BFIO_FILE) {
    return wad_pos;
  }
  return 0;
}

int fseek(FILE *fp, int offset, int whence) {
  if (fp != BFIO_FILE) {
    return 0;
  }

  if (whence == SEEK_SET) {
    wad_pos = offset;
  } else if (whence == SEEK_CUR) {
    wad_pos += offset;
  } else if (whence == SEEK_END) {
    wad_pos = wad_size + offset;
  }

  if (wad_pos < 0) {
    wad_pos = 0;
  }
  if (wad_pos > wad_size) {
    wad_pos = wad_size;
  }

  return 0;
}

size_t fread(void *ptr, size_t size, size_t nmemb, FILE *fp) {
  char *out = ptr;
  int total = size * nmemb;
  int got = 0;

  if (size == 0 || nmemb == 0) {
    return 0;
  }

  if (fp != BFIO_FILE) {
    for (; got < total; got++) {
      int c = fgetc(fp);
      if (c == EOF) {
        break;
      }
      out[got] = c;
    }
    return got / size;
  }

  if (wad_pos + total > wad_size) {
    total = wad_size - wad_pos;
  }

  bfio_prefix('D');
  bfio_send_u24(wad_pos);
  bfio_send_u24(total);
  bfio_send_u24((int) out);

  wad_pos += total;
  return total / size;
}

size_t fwrite(const void *ptr, size_t size, size_t nmemb, FILE *fp) {
  const char *p = ptr;
  int total = size * nmemb;
  (void) fp;
  for (int i = 0; i < total; i++) {
    putchar(p[i]);
  }
  return nmemb;
}

int fputs(const char *s, FILE *fp) {
  (void) fp;
  print_str(s);
  return 0;
}

int fgetc(FILE *fp) {
  int r;
  (void) fp;
  if (g_ungot == EOF) {
    if (eof_seen) {
      return EOF;
    }
    r = getchar();
    eof_seen = r == EOF;
    return r;
  }
  r = g_ungot;
  g_ungot = EOF;
  return r;
}

int getc(FILE *fp) {
  return fgetc(fp);
}

int ungetc(int c, FILE *fp) {
  (void) fp;
  if (g_ungot == EOF) {
    g_ungot = c;
    return c;
  }
  return EOF;
}

int fputc(int c, FILE *fp) {
  (void) fp;
  return putchar(c);
}

int putc(int c, FILE *fp) {
  return fputc(c, fp);
}

char *fgets(char *s, int size, FILE *fp) {
  int i = 0;
  if (size <= 0) {
    return NULL;
  }
  while (i < size - 1) {
    int c = fgetc(fp);
    if (c == EOF) {
      break;
    }
    s[i++] = c;
    if (c == '\n') {
      break;
    }
  }
  if (i == 0) {
    return NULL;
  }
  s[i] = 0;
  return s;
}

int bfhost_poll_key(int *pressed, unsigned char *key) {
  bfio_prefix('K');
  int state = bfio_recv_byte();
  int value = bfio_recv_byte();

  if (state == 0) {
    return 0;
  }

  *pressed = state == 1;
  *key = value & 255;
  return 1;
}

void bfhost_load_wad_directory(void *dest, int dir_offset, int count,
                               void *wad_file, int stride, int wad_file_off,
                               int position_off, int size_off, int cache_off,
                               int next_off) {
  bfio_prefix('L');
  bfio_send_u24(dir_offset);
  bfio_send_u24(count);
  bfio_send_u24((int) dest);
  bfio_send_u24((int) wad_file);
  bfio_send_u24(stride);
  bfio_send_u24(wad_file_off);
  bfio_send_u24(position_off);
  bfio_send_u24(size_off);
  bfio_send_u24(cache_off);
  bfio_send_u24(next_off);
}

void bfhost_load_patch_headers(int first, int count, int *widths,
                               int *offsets, int *tops) {
  bfio_prefix('H');
  bfio_send_u24(first);
  bfio_send_u24(count);
  bfio_send_u24((int) widths);
  bfio_send_u24((int) offsets);
  bfio_send_u24((int) tops);
}

void bfhost_load_patch_lookup(int count, int *lookup) {
  bfio_prefix('J');
  bfio_send_u24(count);
  bfio_send_u24((int) lookup);
}

void bfhost_generate_texture_lookup(int count, void *textures,
                                    void *columnlump, void *columnofs,
                                    void *texturecomposite,
                                    void *compositesize, int width_off,
                                    int height_off, int patchcount_off,
                                    int patches_off, int originx_off,
                                    int patch_off, int patch_stride) {
  bfio_prefix('G');
  bfio_send_u24(count);
  bfio_send_u24((int) textures);
  bfio_send_u24((int) columnlump);
  bfio_send_u24((int) columnofs);
  bfio_send_u24((int) texturecomposite);
  bfio_send_u24((int) compositesize);
  bfio_send_u24(width_off);
  bfio_send_u24(height_off);
  bfio_send_u24(patchcount_off);
  bfio_send_u24(patches_off);
  bfio_send_u24(originx_off);
  bfio_send_u24(patch_off);
  bfio_send_u24(patch_stride);
}

void bfhost_load_textures(int count, void *textures, void *texturecolumnlump,
                          void *texturecolumnofs, void *texturecomposite,
                          void *texturecompositesize, void *texturewidthmask,
                          void *textureheight, void *texturetranslation,
                          void *texturepool, int texture_stride,
                          void *column_lump_pool, void *column_ofs_pool,
                          int column_stride, int width_off, int height_off,
                          int patchcount_off, int name_off, int index_off,
                          int next_off, int patches_off, int originx_off,
                          int originy_off, int patch_off, int patch_stride) {
  bfio_prefix('t');
  bfio_send_u24(count);
  bfio_send_u24((int) textures);
  bfio_send_u24((int) texturecolumnlump);
  bfio_send_u24((int) texturecolumnofs);
  bfio_send_u24((int) texturecomposite);
  bfio_send_u24((int) texturecompositesize);
  bfio_send_u24((int) texturewidthmask);
  bfio_send_u24((int) textureheight);
  bfio_send_u24((int) texturetranslation);
  bfio_send_u24((int) texturepool);
  bfio_send_u24(texture_stride);
  bfio_send_u24((int) column_lump_pool);
  bfio_send_u24((int) column_ofs_pool);
  bfio_send_u24(column_stride);
  bfio_send_u24(width_off);
  bfio_send_u24(height_off);
  bfio_send_u24(patchcount_off);
  bfio_send_u24(name_off);
  bfio_send_u24(index_off);
  bfio_send_u24(next_off);
  bfio_send_u24(patches_off);
  bfio_send_u24(originx_off);
  bfio_send_u24(originy_off);
  bfio_send_u24(patch_off);
  bfio_send_u24(patch_stride);
}

void bfhost_fill_words(int *dest, int count, int value) {
  bfio_prefix('F');
  bfio_send_u24((int) dest);
  bfio_send_u24(count);
  bfio_send_u24(value);
}

void bfhost_init_light_tables(void *zlight, void *colormaps,
                              int lightlevels, int maxlightz,
                              int numcolormaps) {
  bfio_prefix('l');
  bfio_send_u24((int) zlight);
  bfio_send_u24((int) colormaps);
  bfio_send_u24(lightlevels);
  bfio_send_u24(maxlightz);
  bfio_send_u24(numcolormaps);
}

void bfhost_init_view_tables(void *viewangletox, void *xtoviewangle,
                             void *yslope, void *distscale,
                             void *scalelight, void *colormaps,
                             void *finetangent, void *finecosine,
                             int centerxfrac, int centerx, int viewwidth,
                             int viewheight, int detailshift, int fineangles,
                             int fieldofview, int fracunit, int fracbits,
                             int ang90, int angletofine, int screenwidth,
                             int lightlevels, int maxlightscale,
                             int numcolormaps, int distmap) {
  bfio_prefix('w');
  bfio_send_u24((int) viewangletox);
  bfio_send_u24((int) xtoviewangle);
  bfio_send_u24((int) yslope);
  bfio_send_u24((int) distscale);
  bfio_send_u24((int) scalelight);
  bfio_send_u24((int) colormaps);
  bfio_send_u24((int) finetangent);
  bfio_send_u24((int) finecosine);
  bfio_send_u24(centerxfrac);
  bfio_send_u24(centerx);
  bfio_send_u24(viewwidth);
  bfio_send_u24(viewheight);
  bfio_send_u24(detailshift);
  bfio_send_u24(fineangles);
  bfio_send_u24(fieldofview);
  bfio_send_u24(fracunit);
  bfio_send_u24(fracbits);
  bfio_send_u24(ang90);
  bfio_send_u24(angletofine);
  bfio_send_u24(screenwidth);
  bfio_send_u24(lightlevels);
  bfio_send_u24(maxlightscale);
  bfio_send_u24(numcolormaps);
  bfio_send_u24(distmap);
}

void bfhost_init_translation_tables(void *translationtables) {
  bfio_prefix('r');
  bfio_send_u24((int) translationtables);
}

void bfhost_render_map_view(void *screen, void *mobj, void *lines,
                            int numlines, int line_stride, int line_v1_off,
                            int line_v2_off, int vertex_stride,
                            int vertex_x_off, int vertex_y_off,
                            int mobj_x_off, int mobj_y_off,
                            int mobj_angle_off, int width, int height) {
  bfio_prefix('o');
  bfio_send_u24((int) screen);
  bfio_send_u24((int) mobj);
  bfio_send_u24((int) lines);
  bfio_send_u24(numlines);
  bfio_send_u24(line_stride);
  bfio_send_u24(line_v1_off);
  bfio_send_u24(line_v2_off);
  bfio_send_u24(vertex_stride);
  bfio_send_u24(vertex_x_off);
  bfio_send_u24(vertex_y_off);
  bfio_send_u24(mobj_x_off);
  bfio_send_u24(mobj_y_off);
  bfio_send_u24(mobj_angle_off);
  bfio_send_u24(width);
  bfio_send_u24(height);
}

void bfhost_prepare_sprite_defs(void *namelist, int numsprites,
                                int firstsprite, int lastsprite,
                                int modified, int *maxframes) {
  bfio_prefix('Y');
  bfio_send_u24((int) namelist);
  bfio_send_u24(numsprites);
  bfio_send_u24(firstsprite);
  bfio_send_u24(lastsprite);
  bfio_send_u24(modified);
  bfio_send_u24((int) maxframes);
}

void bfhost_fill_sprite_defs(void *sprites, int numsprites,
                             int spritedef_stride, int spriteframes_off,
                             int spriteframe_stride, int rotate_off,
                             int lump_off, int flip_off) {
  bfio_prefix('Z');
  bfio_send_u24((int) sprites);
  bfio_send_u24(numsprites);
  bfio_send_u24(spritedef_stride);
  bfio_send_u24(spriteframes_off);
  bfio_send_u24(spriteframe_stride);
  bfio_send_u24(rotate_off);
  bfio_send_u24(lump_off);
  bfio_send_u24(flip_off);
}

void bfhost_draw_frame(void *screen, void *colors, int width, int height,
                       int scale, int color_stride, int r_off, int g_off,
                       int b_off) {
  bfio_prefix('Q');
  bfio_send_u24((int) screen);
  bfio_send_u24((int) colors);
  bfio_send_u24(width);
  bfio_send_u24(height);
  bfio_send_u24(scale);
  bfio_send_u24(color_stride);
  bfio_send_u24(r_off);
  bfio_send_u24(g_off);
  bfio_send_u24(b_off);
}

void bfhost_load_hu_fonts(void *font, void *storage, int first, int count) {
  bfio_prefix('U');
  bfio_send_u24((int) font);
  bfio_send_u24((int) storage);
  bfio_send_u24(first);
  bfio_send_u24(count);
}

void bfhost_load_status_patches(void *storage, void *tallnum, void *shortnum,
                                void *tallpercent, void *keys, void *armsbg,
                                void *arms, void *faceback, void *sbar,
                                void *faces, int consoleplayer) {
  bfio_prefix('O');
  bfio_send_u24((int) storage);
  bfio_send_u24((int) tallnum);
  bfio_send_u24((int) shortnum);
  bfio_send_u24((int) tallpercent);
  bfio_send_u24((int) keys);
  bfio_send_u24((int) armsbg);
  bfio_send_u24((int) arms);
  bfio_send_u24((int) faceback);
  bfio_send_u24((int) sbar);
  bfio_send_u24((int) faces);
  bfio_send_u24(consoleplayer);
}

void bfhost_load_map_vertexes(int lump, void *vertexes, int count,
                              int vertex_stride, int x_off, int y_off) {
  bfio_prefix('v');
  bfio_send_u24(lump);
  bfio_send_u24((int) vertexes);
  bfio_send_u24(count);
  bfio_send_u24(vertex_stride);
  bfio_send_u24(x_off);
  bfio_send_u24(y_off);
}

void bfhost_load_map_sectors(int lump, void *sectors, int count, int firstflat,
                             int stride, int floorheight_off,
                             int ceilingheight_off, int floorpic_off,
                             int ceilingpic_off, int lightlevel_off,
                             int special_off, int tag_off,
                             int thinglist_off) {
  bfio_prefix('A');
  bfio_send_u24(lump);
  bfio_send_u24((int) sectors);
  bfio_send_u24(count);
  bfio_send_u24(firstflat);
  bfio_send_u24(stride);
  bfio_send_u24(floorheight_off);
  bfio_send_u24(ceilingheight_off);
  bfio_send_u24(floorpic_off);
  bfio_send_u24(ceilingpic_off);
  bfio_send_u24(lightlevel_off);
  bfio_send_u24(special_off);
  bfio_send_u24(tag_off);
  bfio_send_u24(thinglist_off);
}

void bfhost_load_map_sides(int lump, void *sides, int count, void *sectors,
                           int sector_stride, int side_stride,
                           int textureoffset_off, int rowoffset_off,
                           int toptexture_off, int bottomtexture_off,
                           int midtexture_off, int sector_off) {
  bfio_prefix('B');
  bfio_send_u24(lump);
  bfio_send_u24((int) sides);
  bfio_send_u24(count);
  bfio_send_u24((int) sectors);
  bfio_send_u24(sector_stride);
  bfio_send_u24(side_stride);
  bfio_send_u24(textureoffset_off);
  bfio_send_u24(rowoffset_off);
  bfio_send_u24(toptexture_off);
  bfio_send_u24(bottomtexture_off);
  bfio_send_u24(midtexture_off);
  bfio_send_u24(sector_off);
}

void bfhost_load_map_lines(int lump, void *lines, int count, void *vertexes,
                           int vertex_stride, int vertex_x_off,
                           int vertex_y_off, void *sides, int side_stride,
                           int side_sector_off, int line_stride, int v1_off,
                           int v2_off, int dx_off, int dy_off, int flags_off,
                           int special_off, int tag_off, int sidenum_off,
                           int bbox_off, int slopetype_off,
                           int frontsector_off, int backsector_off) {
  bfio_prefix('C');
  bfio_send_u24(lump);
  bfio_send_u24((int) lines);
  bfio_send_u24(count);
  bfio_send_u24((int) vertexes);
  bfio_send_u24(vertex_stride);
  bfio_send_u24(vertex_x_off);
  bfio_send_u24(vertex_y_off);
  bfio_send_u24((int) sides);
  bfio_send_u24(side_stride);
  bfio_send_u24(side_sector_off);
  bfio_send_u24(line_stride);
  bfio_send_u24(v1_off);
  bfio_send_u24(v2_off);
  bfio_send_u24(dx_off);
  bfio_send_u24(dy_off);
  bfio_send_u24(flags_off);
  bfio_send_u24(special_off);
  bfio_send_u24(tag_off);
  bfio_send_u24(sidenum_off);
  bfio_send_u24(bbox_off);
  bfio_send_u24(slopetype_off);
  bfio_send_u24(frontsector_off);
  bfio_send_u24(backsector_off);
}

void bfhost_load_map_nodes(int lump, void *nodes, int count, int node_stride,
                           int x_off, int y_off, int dx_off, int dy_off,
                           int bbox_off, int children_off) {
  bfio_prefix('E');
  bfio_send_u24(lump);
  bfio_send_u24((int) nodes);
  bfio_send_u24(count);
  bfio_send_u24(node_stride);
  bfio_send_u24(x_off);
  bfio_send_u24(y_off);
  bfio_send_u24(dx_off);
  bfio_send_u24(dy_off);
  bfio_send_u24(bbox_off);
  bfio_send_u24(children_off);
}

void bfhost_load_map_segs(int lump, void *segs, int count, void *vertexes,
                          int vertex_stride, void *sides, int side_stride,
                          int side_sector_off, int numsides, void *lines,
                          int line_stride, int line_flags_off,
                          int line_sidenum_off, int seg_stride, int v1_off,
                          int v2_off, int offset_off, int angle_off,
                          int sidedef_off, int linedef_off,
                          int frontsector_off, int backsector_off) {
  bfio_prefix('I');
  bfio_send_u24(lump);
  bfio_send_u24((int) segs);
  bfio_send_u24(count);
  bfio_send_u24((int) vertexes);
  bfio_send_u24(vertex_stride);
  bfio_send_u24((int) sides);
  bfio_send_u24(side_stride);
  bfio_send_u24(side_sector_off);
  bfio_send_u24(numsides);
  bfio_send_u24((int) lines);
  bfio_send_u24(line_stride);
  bfio_send_u24(line_flags_off);
  bfio_send_u24(line_sidenum_off);
  bfio_send_u24(seg_stride);
  bfio_send_u24(v1_off);
  bfio_send_u24(v2_off);
  bfio_send_u24(offset_off);
  bfio_send_u24(angle_off);
  bfio_send_u24(sidedef_off);
  bfio_send_u24(linedef_off);
  bfio_send_u24(frontsector_off);
  bfio_send_u24(backsector_off);
}

int bfhost_player_start_field(int lump, int field) {
  bfio_prefix('n');
  bfio_send_u24(lump);
  bfio_send_u24(field);
  return bfio_recv_u24();
}

void bfhost_group_lines(void *sectors, int numsectors, void *subsectors,
                        int numsubsectors, void *segs, int seg_stride,
                        int seg_frontsector_off, void *lines, int numlines,
                        int line_stride, int line_frontsector_off,
                        int line_backsector_off, int line_bbox_off,
                        void *linebuffer, int *totallines_out,
                        int sector_stride, int sector_linecount_off,
                        int sector_lines_off, int sector_blockbox_off,
                        int sector_soundorg_x_off, int sector_soundorg_y_off,
                        int subsector_stride, int subsector_firstline_off,
                        int subsector_sector_off, int bmaporgx_value,
                        int bmaporgy_value, int bmapwidth_value,
                        int bmapheight_value, int maxradius_value,
                        int mapblockshift_value) {
  bfio_prefix('V');
  bfio_send_u24((int) sectors);
  bfio_send_u24(numsectors);
  bfio_send_u24((int) subsectors);
  bfio_send_u24(numsubsectors);
  bfio_send_u24((int) segs);
  bfio_send_u24(seg_stride);
  bfio_send_u24(seg_frontsector_off);
  bfio_send_u24((int) lines);
  bfio_send_u24(numlines);
  bfio_send_u24(line_stride);
  bfio_send_u24(line_frontsector_off);
  bfio_send_u24(line_backsector_off);
  bfio_send_u24(line_bbox_off);
  bfio_send_u24((int) linebuffer);
  bfio_send_u24((int) totallines_out);
  bfio_send_u24(sector_stride);
  bfio_send_u24(sector_linecount_off);
  bfio_send_u24(sector_lines_off);
  bfio_send_u24(sector_blockbox_off);
  bfio_send_u24(sector_soundorg_x_off);
  bfio_send_u24(sector_soundorg_y_off);
  bfio_send_u24(subsector_stride);
  bfio_send_u24(subsector_firstline_off);
  bfio_send_u24(subsector_sector_off);
  bfio_send_u24(bmaporgx_value);
  bfio_send_u24(bmaporgy_value);
  bfio_send_u24(bmapwidth_value);
  bfio_send_u24(bmapheight_value);
  bfio_send_u24(maxradius_value);
  bfio_send_u24(mapblockshift_value);
}

void *bfhost_point_in_subsector(int x, int y, void *nodes, int numnodes,
                                int node_stride, int node_x_off,
                                int node_y_off, int node_dx_off,
                                int node_dy_off, int node_children_off,
                                void *subsectors, int subsector_stride,
                                int nf_subsector) {
  bfio_prefix('X');
  bfio_send_u24(x);
  bfio_send_u24(y);
  bfio_send_u24((int) nodes);
  bfio_send_u24(numnodes);
  bfio_send_u24(node_stride);
  bfio_send_u24(node_x_off);
  bfio_send_u24(node_y_off);
  bfio_send_u24(node_dx_off);
  bfio_send_u24(node_dy_off);
  bfio_send_u24(node_children_off);
  bfio_send_u24((int) subsectors);
  bfio_send_u24(subsector_stride);
  bfio_send_u24(nf_subsector);
  return (void *) bfio_recv_u24();
}

int bfhost_mobj_type_for_doomednum(int doomednum, void *mobjinfo, int count,
                                   int stride, int doomednum_off) {
  bfio_prefix('M');
  bfio_send_u24(doomednum);
  bfio_send_u24((int) mobjinfo);
  bfio_send_u24(count);
  bfio_send_u24(stride);
  bfio_send_u24(doomednum_off);
  int result = bfio_recv_u24();
  return result == 0 ? -1 : result - 1;
}

void *bfhost_z_malloc(void *zone, int size, int tag, void *user,
                      int mem_align, int memblock_size, int minfragment,
                      int zoneid, int pu_free, int pu_purgelevel,
                      int zone_rover_off, int block_size_off,
                      int block_user_off, int block_tag_off, int block_id_off,
                      int block_next_off, int block_prev_off) {
  bfio_prefix('z');
  bfio_send_u24((int) zone);
  bfio_send_u24(size);
  bfio_send_u24(tag);
  bfio_send_u24((int) user);
  bfio_send_u24(mem_align);
  bfio_send_u24(memblock_size);
  bfio_send_u24(minfragment);
  bfio_send_u24(zoneid);
  bfio_send_u24(pu_free);
  bfio_send_u24(pu_purgelevel);
  bfio_send_u24(zone_rover_off);
  bfio_send_u24(block_size_off);
  bfio_send_u24(block_user_off);
  bfio_send_u24(block_tag_off);
  bfio_send_u24(block_id_off);
  bfio_send_u24(block_next_off);
  bfio_send_u24(block_prev_off);
  return (void *) bfio_recv_u24();
}

void bfhost_set_thing_position(void *thing, void *nodes, int numnodes,
                               int node_stride, int node_x_off,
                               int node_y_off, int node_dx_off,
                               int node_dy_off, int node_children_off,
                               void *subsectors, int subsector_stride,
                               int subsector_sector_off, void *blocklinks,
                               int bmaporgx_value, int bmaporgy_value,
                               int bmapwidth_value, int bmapheight_value,
                               int mapblockshift_value, int mf_nosector,
                               int mf_noblockmap, int thing_x_off,
                               int thing_y_off, int thing_flags_off,
                               int thing_subsector_off, int thing_snext_off,
                               int thing_sprev_off, int thing_bnext_off,
                               int thing_bprev_off, int sector_thinglist_off,
                               int nf_subsector) {
  bfio_prefix('p');
  bfio_send_u24((int) thing);
  bfio_send_u24((int) nodes);
  bfio_send_u24(numnodes);
  bfio_send_u24(node_stride);
  bfio_send_u24(node_x_off);
  bfio_send_u24(node_y_off);
  bfio_send_u24(node_dx_off);
  bfio_send_u24(node_dy_off);
  bfio_send_u24(node_children_off);
  bfio_send_u24((int) subsectors);
  bfio_send_u24(subsector_stride);
  bfio_send_u24(subsector_sector_off);
  bfio_send_u24((int) blocklinks);
  bfio_send_u24(bmaporgx_value);
  bfio_send_u24(bmaporgy_value);
  bfio_send_u24(bmapwidth_value);
  bfio_send_u24(bmapheight_value);
  bfio_send_u24(mapblockshift_value);
  bfio_send_u24(mf_nosector);
  bfio_send_u24(mf_noblockmap);
  bfio_send_u24(thing_x_off);
  bfio_send_u24(thing_y_off);
  bfio_send_u24(thing_flags_off);
  bfio_send_u24(thing_subsector_off);
  bfio_send_u24(thing_snext_off);
  bfio_send_u24(thing_sprev_off);
  bfio_send_u24(thing_bnext_off);
  bfio_send_u24(thing_bprev_off);
  bfio_send_u24(sector_thinglist_off);
  bfio_send_u24(nf_subsector);
}

void bfhost_spawn_mobj(void *mobj, int mobj_size, int x, int y, int z,
                       int type, void *mobjinfo, int info_stride,
                       int info_spawnstate_off, int info_spawnhealth_off,
                       int info_reactiontime_off, int info_radius_off,
                       int info_height_off, int info_flags_off, void *states,
                       int state_stride, int state_sprite_off,
                       int state_frame_off, int state_tics_off, void *nodes,
                       int numnodes, int node_stride, int node_x_off,
                       int node_y_off, int node_dx_off, int node_dy_off,
                       int node_children_off, void *subsectors,
                       int subsector_stride, int subsector_sector_off,
                       void *blocklinks, int bmaporgx_value,
                       int bmaporgy_value, int bmapwidth_value,
                       int bmapheight_value, int mapblockshift_value,
                       int mf_nosector, int mf_noblockmap, int nf_subsector,
                       int mobj_x_off, int mobj_y_off, int mobj_z_off,
                       int mobj_type_off, int mobj_info_off,
                       int mobj_radius_off, int mobj_height_off,
                       int mobj_flags_off, int mobj_health_off,
                       int mobj_reactiontime_off, int mobj_lastlook_off,
                       int mobj_state_off, int mobj_tics_off,
                       int mobj_sprite_off, int mobj_frame_off,
                       int mobj_subsector_off, int mobj_floorz_off,
                       int mobj_ceilingz_off, int mobj_snext_off,
                       int mobj_sprev_off, int mobj_bnext_off,
                       int mobj_bprev_off, int sector_thinglist_off,
                       int sector_floorheight_off, int sector_ceilingheight_off,
                       int onfloorz, int onceilingz) {
  bfio_prefix('m');
  bfio_send_u24((int) mobj);
  bfio_send_u24(mobj_size);
  bfio_send_u24(x);
  bfio_send_u24(y);
  bfio_send_u24(z);
  bfio_send_u24(type);
  bfio_send_u24((int) mobjinfo);
  bfio_send_u24(info_stride);
  bfio_send_u24(info_spawnstate_off);
  bfio_send_u24(info_spawnhealth_off);
  bfio_send_u24(info_reactiontime_off);
  bfio_send_u24(info_radius_off);
  bfio_send_u24(info_height_off);
  bfio_send_u24(info_flags_off);
  bfio_send_u24((int) states);
  bfio_send_u24(state_stride);
  bfio_send_u24(state_sprite_off);
  bfio_send_u24(state_frame_off);
  bfio_send_u24(state_tics_off);
  bfio_send_u24((int) nodes);
  bfio_send_u24(numnodes);
  bfio_send_u24(node_stride);
  bfio_send_u24(node_x_off);
  bfio_send_u24(node_y_off);
  bfio_send_u24(node_dx_off);
  bfio_send_u24(node_dy_off);
  bfio_send_u24(node_children_off);
  bfio_send_u24((int) subsectors);
  bfio_send_u24(subsector_stride);
  bfio_send_u24(subsector_sector_off);
  bfio_send_u24((int) blocklinks);
  bfio_send_u24(bmaporgx_value);
  bfio_send_u24(bmaporgy_value);
  bfio_send_u24(bmapwidth_value);
  bfio_send_u24(bmapheight_value);
  bfio_send_u24(mapblockshift_value);
  bfio_send_u24(mf_nosector);
  bfio_send_u24(mf_noblockmap);
  bfio_send_u24(nf_subsector);
  bfio_send_u24(mobj_x_off);
  bfio_send_u24(mobj_y_off);
  bfio_send_u24(mobj_z_off);
  bfio_send_u24(mobj_type_off);
  bfio_send_u24(mobj_info_off);
  bfio_send_u24(mobj_radius_off);
  bfio_send_u24(mobj_height_off);
  bfio_send_u24(mobj_flags_off);
  bfio_send_u24(mobj_health_off);
  bfio_send_u24(mobj_reactiontime_off);
  bfio_send_u24(mobj_lastlook_off);
  bfio_send_u24(mobj_state_off);
  bfio_send_u24(mobj_tics_off);
  bfio_send_u24(mobj_sprite_off);
  bfio_send_u24(mobj_frame_off);
  bfio_send_u24(mobj_subsector_off);
  bfio_send_u24(mobj_floorz_off);
  bfio_send_u24(mobj_ceilingz_off);
  bfio_send_u24(mobj_snext_off);
  bfio_send_u24(mobj_sprev_off);
  bfio_send_u24(mobj_bnext_off);
  bfio_send_u24(mobj_bprev_off);
  bfio_send_u24(sector_thinglist_off);
  bfio_send_u24(sector_floorheight_off);
  bfio_send_u24(sector_ceilingheight_off);
  bfio_send_u24(onfloorz);
  bfio_send_u24(onceilingz);
}

int bfhost_wad_lump_token(const char *name) {
  bfio_prefix('N');
  bfio_send_cstr(name);
  return bfio_recv_u24();
}

int bfhost_texture_index(const char *name) {
  int result;
  bfio_prefix('T');
  bfio_send_cstr(name);
  result = bfio_recv_u24();
  return result == 0 ? -1 : result - 1;
}

int bfhost_wad_lump_present(const char *name) {
  bfio_prefix('P');
  bfio_send_cstr(name);
  return bfio_recv_byte();
}

int bfhost_wad_lump_index(const char *name) {
  int result = bfhost_wad_lump_token(name);

  if (result == 0) {
    return -1;
  }

  return result - 1;
}

char *strrchr(const char *s, int c) {
  const char *last = NULL;
  do {
    if (*s == c) {
      last = s;
    }
  } while (*s++);
  return (char *) last;
}

void *memmove(void *dst, const void *src, size_t n) {
  char *d = dst;
  const char *s = src;
  if (d < s) {
    for (size_t i = 0; i < n; i++) {
      d[i] = s[i];
    }
  } else if (d > s) {
    while (n) {
      n--;
      d[n] = s[n];
    }
  }
  return dst;
}

char *strstr(const char *haystack, const char *needle) {
  if (!*needle) {
    return (char *) haystack;
  }
  for (; *haystack; haystack++) {
    const char *h = haystack;
    const char *n = needle;
    while (*h && *n && *h == *n) {
      h++;
      n++;
    }
    if (!*n) {
      return (char *) haystack;
    }
  }
  return NULL;
}

char *strncpy(char *dst, const char *src, size_t n) {
  size_t i = 0;
  for (; i < n && src[i]; i++) {
    dst[i] = src[i];
  }
  for (; i < n; i++) {
    dst[i] = 0;
  }
  return dst;
}

int abs(int x) {
  return x < 0 ? -x : x;
}

int fabs(int x) {
  return abs(x);
}

void *realloc(void *ptr, size_t size) {
  void *next = malloc(size);
  if (ptr && next) {
    memcpy(next, ptr, size);
  }
  return next;
}

int system(const char *command) {
  (void) command;
  return 0;
}

int sscanf(const char *str, const char *fmt) {
  (void) str;
  (void) fmt;
  return 0;
}

int atof(const char *str) {
  return atoi(str);
}

int mkdir(const char *path) {
  (void) path;
  return 0;
}

int remove(const char *path) {
  (void) path;
  return 0;
}

int rename(const char *oldpath, const char *newpath) {
  (void) oldpath;
  (void) newpath;
  return 0;
}

int ioctl(int fd, unsigned long request) {
  (void) fd;
  (void) request;
  return 0;
}

int stat(const char *path, struct stat *buf) {
  int size = bfio_path_size(path);
  if (size <= 0) {
    return -1;
  }
  if (buf) {
    buf->st_size = size;
  }
  return 0;
}

int open(const char *path, int flags, ...) {
  int size = bfio_path_size(path);
  (void) flags;
  if (size <= 0) {
    return -1;
  }
  wad_size = size;
  wad_pos = 0;
  return 2;
}

int read(int fd, void *buf, int count) {
  if (fd != 2) {
    return 0;
  }
  return fread(buf, 1, count, BFIO_FILE);
}

int write(int fd, const void *buf, int count) {
  const char *p = buf;
  (void) fd;
  for (int i = 0; i < count; i++) {
    putchar(p[i]);
  }
  return count;
}

int close(int fd) {
  (void) fd;
  return 0;
}
