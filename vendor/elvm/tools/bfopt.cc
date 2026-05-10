#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <deque>
#include <fstream>
#include <iterator>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include <sys/select.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <termios.h>
#include <unistd.h>

#ifdef __GNUC__
#if __has_attribute(fallthrough)
#define FALLTHROUGH __attribute__((fallthrough))
#else
#define FALLTHROUGH
#endif
#else
#define FALLTHROUGH
#endif

typedef unsigned char byte;

using namespace std;

bool g_trace;
bool g_verbose;
bool g_doom_host;
bool g_window_stream;
string g_wad_path = "data/DOOM1.WAD";
string g_capture_path = "build/bfdoom-first-frame.ppm";
string g_program_path;

static const int kFrameWidth = 640;
static const int kFrameHeight = 400;
static const int kFrameBytes = kFrameWidth * kFrameHeight * 3;
static const int kElvmMem = 70;
static const int kElvmMemCtlLen = 16;
static const int kElvmMemBlkLen = 256 * 3 + kElvmMemCtlLen;

vector<byte> g_wad;
vector<string> g_wad_lumps;
vector<int> g_wad_positions;
vector<int> g_wad_sizes;
vector<string> g_texture_names;
deque<byte> g_input_queue;
deque<pair<byte, byte> > g_key_events;
vector<byte>* g_active_mem;
struct termios g_old_termios;
bool g_raw_terminal;
bool g_terminal_started;
int g_frame_count;
size_t g_run_pc;
char g_run_op;
int g_run_arg;
bool g_snapshot_due;
bool g_snapshot_saved;
bool g_snapshot_loaded;
vector<byte> g_last_render_args;
vector<byte> g_last_draw_args;

struct HostActor {
  double x;
  double y;
  int angle;
  int type;
  int options;
  int health;
  int flash;
  int attack_tics;
  int death_tics;
  bool ambush;
  bool awake;
  bool alive;
};

struct HostLine {
  double x1;
  double y1;
  double x2;
  double y2;
  string texture;
  int color_base;
  int light;
  int flags;
  int front_floor;
  int front_ceiling;
  int back_floor;
  int back_ceiling;
  int special;
  int tag;
  bool solid;
  bool blocks_move;
  bool door;
  bool exit;
};

struct HostTexture {
  int width;
  int height;
  vector<byte> pixels;
};

vector<HostActor> g_host_actors;
vector<HostLine> g_host_lines;
map<string, HostTexture> g_host_texture_cache;
map<string, HostTexture> g_host_flat_cache;
vector<int> g_host_patch_lookup;
bool g_host_actors_ready;
bool g_host_map_ready;
bool g_host_player_ready;
int g_host_actor_spawned;
int g_host_actor_skipped_skill;
int g_host_actor_skipped_single;
int g_host_actor_ambush;
string g_host_floor_flat = "FLOOR4_8";
string g_host_ceiling_flat = "CEIL3_5";
bool g_host_has_sky = false;
string g_host_map_name = "E1M1";
int g_host_episode = 1;
int g_host_map = 1;
double g_host_player_x;
double g_host_player_y;
double g_host_player_angle;
int g_host_fire_flash;
int g_host_weapon = 2;
bool g_host_weapon_owned[8] = {false, false, true, false,
                               false, false, false, false};
enum HostAmmoType {
  HOST_AMMO_BULLET = 0,
  HOST_AMMO_SHELL = 1,
  HOST_AMMO_ROCKET = 2,
  HOST_AMMO_CELL = 3,
  HOST_AMMO_COUNT = 4,
};
int g_host_ammo[HOST_AMMO_COUNT] = {50, 0, 0, 0};
int g_host_ammo_max[HOST_AMMO_COUNT] = {200, 50, 50, 300};
int g_host_health = 100;
int g_host_armor = 0;
int g_host_tick;
int g_host_prndindex;
int g_host_damage_flash;
int g_host_pickup_flash;
bool g_host_level_complete;
bool g_host_logged_rotated_sprite;
bool g_host_logged_animated_sprite;
bool g_host_logged_attack_sprite;
bool g_host_logged_pain_sprite;
bool g_host_logged_death_sprite;

struct HostRenderedActor {
  int index;
  double dist;
  int x0;
  int x1;
};

vector<HostRenderedActor> g_host_rendered_actors;
int g_host_render_width;

struct HostSpriteFrame {
  int rotate;
  int lump[8];
  int flip[8];
};

vector<vector<HostSpriteFrame> > g_prepared_sprites;

struct Loop;

enum OpType {
  OP_MEM,
  OP_PTR,
  OP_LOOP,
  OP_COMMENT,
};

struct Op {
  char op;
  int arg;
  Loop* loop;
  string comment;

  Op()
      : op(0), arg(0) {
  }
};

struct Loop {
  vector<Op*> code;
  map<int, int> addsub;
  int ptr;
  bool has_io;

  Loop() {
    reset(NULL);
  }

  void reset(vector<Op*>* out) {
    if (out)
      copy(code.begin(), code.end(), back_inserter(*out));

    code.clear();
    addsub.clear();
    ptr = 0;
    has_io = false;
  }
};

struct LoopTerm {
  int rel;
  int delta;
};

struct FastOp {
  unsigned char op;
  int arg;
  int aux;
  const char* comment;
};

static const unsigned int kFastCacheMagic = 0x42464f43;
static const unsigned int kFastCacheVersion = 3;
static const unsigned int kSnapshotMagic = 0x4246534e;
static const unsigned int kSnapshotVersion = 2;
static const double kPi = 3.14159265358979323846;
static const unsigned char kDoomRndTable[256] = {
    0,   8, 109, 220, 222, 241, 149, 107,  75, 248, 254, 140,  16,  66,
    74,  21, 211,  47,  80, 242, 154,  27, 205, 128, 161,  89,  77,  36,
    95, 110,  85,  48, 212, 140, 211, 249,  22,  79, 200,  50,  28, 188,
    52, 140, 202, 120,  68, 145,  62,  70, 184, 190,  91, 197, 152, 224,
    149, 104,  25, 178, 252, 182, 202, 182, 141, 197,   4,  81, 181, 242,
    145,  42,  39, 227, 156, 198, 225, 193, 219,  93, 122, 175, 249,   0,
    175, 143,  70, 239,  46, 246, 163,  53, 163, 109, 168, 135,   2, 235,
    25,  92,  20, 145, 138,  77,  69, 166,  78, 176, 173, 212, 166, 113,
    94, 161,  41,  50, 239,  49, 111, 164,  70,  60,   2,  37, 171,  75,
    136, 156,  11,  56,  42, 146, 138, 229,  73, 146,  77,  61,  98, 196,
    135, 106,  63, 197, 195,  86,  96, 203, 113, 101, 170, 247, 181, 113,
    80, 250, 108,   7, 255, 237, 129, 226,  79, 107, 112, 166, 103, 241,
    24, 223, 239, 120, 198,  58,  60,  82, 128,   3, 184,  66, 143, 224,
    145, 224,  81, 206, 163,  45,  63,  90, 168, 114,  59,  33, 159,  95,
    28, 139, 123,  98, 125, 196,  15,  70, 194, 253,  54,  14, 109, 226,
    71,  17, 161,  93, 186,  87, 244, 138,  20,  52, 123, 251,  26,  36,
    17,  46,  52, 231, 232,  76,  31, 221,  84,  37, 216, 165, 212, 106,
    197, 242,  98,  43,  39, 175, 254, 145, 190,  84, 118, 222, 187, 136,
    120, 163, 236, 249,
};

struct CacheFastOp {
  unsigned char op;
  int arg;
  int aux;
};

int merge_ops(char add, char sub, const char** code) {
  int r = 0;
  const char* p;
  for (p = *code; *p; p++) {
    if (*p == add)
      r++;
    else if (*p == sub)
      r--;
    else
      break;
  }
  *code = p - 1;
  return r;
}

void parse(const char* code, vector<Op*>* ops) {
  Loop* cur_loop = new Loop();
  vector<int> loop_stack;
  for (const char* p = code; *p; p++) {
    char c = *p;
    Op* op = new Op();
    switch (c) {
      case '+':
      case '-': {
        op->arg = merge_ops('+', '-', &p);
        if (op->arg) {
          op->op = OP_MEM;
          cur_loop->addsub[cur_loop->ptr] += op->arg;
        } else {
          delete op;
          op = NULL;
        }
        break;
      }

      case '>':
      case '<': {
        op->arg = merge_ops('>', '<', &p);
        if (op->arg) {
          op->op = OP_PTR;
          cur_loop->ptr += op->arg;
        } else {
          delete op;
          op = NULL;
        }
        break;
      }

      case '.':
      case ',': {
        op->op = c;
        cur_loop->has_io = true;
        break;
      }

      case '[': {
        cur_loop->reset(ops);
        op->op = c;
        loop_stack.push_back(ops->size());
        break;
      }

      case ']':
        if (loop_stack.empty()) {
          fprintf(stderr, "unmatched close paren\n");
          exit(1);
        }

        if (!cur_loop->has_io && cur_loop->ptr == 0 &&
            cur_loop->addsub[0] == -1) {
          op->op = OP_LOOP;
          op->loop = cur_loop;
          cur_loop = new Loop();
          cur_loop->has_io = true;
        } else {
          cur_loop->reset(ops);
          op->op = c;
          op->arg = loop_stack.back();
          (*ops)[op->arg]->arg = ops->size();
        }
        loop_stack.pop_back();
        break;

      case '#':
        if (g_trace && p[1] == '{') {
          op->op = OP_COMMENT;
          for (p += 2; *p != '}'; p++) {
            op->comment += *p;
          }
        } else {
          goto nop;
        }
        break;

      case '@':
        if (g_verbose) {
          op->op = c;
          break;
        }
        goto nop;

      default:
      nop:
        delete op;
        op = NULL;
    }

    if (op)
      cur_loop->code.push_back(op);
  }

  cur_loop->reset(ops);

  if (!loop_stack.empty()) {
    fprintf(stderr, "unmatched open paren\n");
    exit(1);
  }
}

void check_bound(int mp) {
  if (mp < 0) {
    fprintf(stderr, "memory pointer out of bound\n");
    exit(1);
  }
}

void alloc_mem(size_t mp, vector<byte>* mem) {
  if (mp >= mem->size()) {
    mem->resize(mp * 2);
  }
}

void ensure_head(int* mp, vector<byte>* mem) {
  if (*mp > 500000000) {
    fprintf(stderr, "right tape runaway: mp=%d pc=%zu op=%d arg=%d size=%zu\n",
            *mp, g_run_pc, g_run_op, g_run_arg, mem->size());
    exit(1);
  }
  if (*mp < 0) {
    size_t grow = max(mem->size(), static_cast<size_t>(-*mp + 1));
    fprintf(stderr, "left tape expand: mp=%d grow=%zu size=%zu\n", *mp, grow, mem->size());
    if (grow > 100000000) {
      fprintf(stderr, "refusing impossible tape grow\n");
      exit(1);
    }
    mem->insert(mem->begin(), grow, 0);
    *mp += grow;
  }
  if (static_cast<size_t>(*mp) >= mem->size()) {
    mem->resize(max(mem->size() * 2, static_cast<size_t>(*mp + 1)));
  }
}

int ensure_relative(int* mp, int rel, vector<byte>* mem) {
  int index = *mp + rel;
  if (index > 500000000) {
    fprintf(stderr, "right tape runaway: mp=%d rel=%d index=%d pc=%zu size=%zu\n",
            *mp, rel, index, g_run_pc, mem->size());
    exit(1);
  }
  if (index < 0) {
    size_t grow = max(mem->size(), static_cast<size_t>(-index + 1));
    fprintf(stderr, "left tape expand: mp=%d rel=%d index=%d grow=%zu size=%zu\n",
            *mp, rel, index, grow, mem->size());
    if (grow > 100000000) {
      fprintf(stderr, "refusing impossible tape grow\n");
      exit(1);
    }
    mem->insert(mem->begin(), grow, 0);
    *mp += grow;
    index += grow;
  }
  if (static_cast<size_t>(index) >= mem->size()) {
    mem->resize(max(mem->size() * 2, static_cast<size_t>(index + 1)));
  }
  return index;
}

static inline void ensure_fast_head(int* mp, vector<byte>* mem) {
  if (static_cast<unsigned>(*mp) < mem->size())
    return;
  ensure_head(mp, mem);
}

void flatten_ops(const vector<Op*>& ops,
                 vector<FastOp>* fast,
                 vector<LoopTerm>* loop_terms) {
  fast->reserve(ops.size());
  for (size_t pc = 0; pc < ops.size(); pc++) {
    const Op* op = ops[pc];
    FastOp out;
    out.op = static_cast<unsigned char>(op->op);
    out.arg = op->arg;
    out.aux = 0;
    out.comment = op->comment.c_str();
    if (op->op == OP_LOOP) {
      out.arg = static_cast<int>(loop_terms->size());
      for (map<int, int>::const_iterator iter = op->loop->addsub.begin();
           iter != op->loop->addsub.end();
           ++iter) {
        if (iter->first == 0)
          continue;
        LoopTerm term;
        term.rel = iter->first;
        term.delta = iter->second;
        loop_terms->push_back(term);
        out.aux++;
      }
    }
    fast->push_back(out);
  }
}

bool load_fast_cache(const string& source_path,
                     vector<FastOp>* fast,
                     vector<LoopTerm>* loop_terms) {
  if (g_trace)
    return false;

  string cache_path = source_path + ".bfocache";
  struct stat source_stat;
  struct stat cache_stat;
  if (stat(source_path.c_str(), &source_stat) != 0 ||
      stat(cache_path.c_str(), &cache_stat) != 0 ||
      cache_stat.st_mtime < source_stat.st_mtime) {
    return false;
  }

  FILE* fp = fopen(cache_path.c_str(), "rb");
  if (!fp)
    return false;

  unsigned int magic = 0;
  unsigned int version = 0;
  unsigned long long fast_count = 0;
  unsigned long long term_count = 0;
  bool ok = fread(&magic, sizeof(magic), 1, fp) == 1 &&
            fread(&version, sizeof(version), 1, fp) == 1 &&
            fread(&fast_count, sizeof(fast_count), 1, fp) == 1 &&
            fread(&term_count, sizeof(term_count), 1, fp) == 1;
  if (!ok || magic != kFastCacheMagic || version != kFastCacheVersion ||
      fast_count > 200000000ULL || term_count > 200000000ULL) {
    fclose(fp);
    return false;
  }

  fast->clear();
  loop_terms->clear();
  vector<CacheFastOp> disk_fast(static_cast<size_t>(fast_count));
  loop_terms->resize(static_cast<size_t>(term_count));

  if (!disk_fast.empty() &&
      fread(&disk_fast[0], sizeof(CacheFastOp), disk_fast.size(), fp) !=
          disk_fast.size()) {
    fclose(fp);
    return false;
  }

  fast->resize(disk_fast.size());
  for (size_t i = 0; i < disk_fast.size(); i++) {
    FastOp& op = (*fast)[i];
    op.op = disk_fast[i].op;
    op.arg = disk_fast[i].arg;
    op.aux = disk_fast[i].aux;
    op.comment = NULL;
  }

  if (!loop_terms->empty() &&
      fread(&(*loop_terms)[0], sizeof(LoopTerm), loop_terms->size(), fp) !=
          loop_terms->size()) {
    fclose(fp);
    return false;
  }

  fclose(fp);
  return true;
}

void save_fast_cache(const string& source_path,
                     const vector<FastOp>& fast,
                     const vector<LoopTerm>& loop_terms) {
  if (g_trace)
    return;

  string cache_path = source_path + ".bfocache";
  string tmp_path = cache_path + ".tmp";
  FILE* fp = fopen(tmp_path.c_str(), "wb");
  if (!fp)
    return;

  unsigned int magic = kFastCacheMagic;
  unsigned int version = kFastCacheVersion;
  unsigned long long fast_count = fast.size();
  unsigned long long term_count = loop_terms.size();
  bool ok = fwrite(&magic, sizeof(magic), 1, fp) == 1 &&
            fwrite(&version, sizeof(version), 1, fp) == 1 &&
            fwrite(&fast_count, sizeof(fast_count), 1, fp) == 1 &&
            fwrite(&term_count, sizeof(term_count), 1, fp) == 1;

  vector<CacheFastOp> disk_fast(fast.size());
  for (size_t i = 0; i < fast.size(); i++) {
    disk_fast[i].op = fast[i].op;
    disk_fast[i].arg = fast[i].arg;
    disk_fast[i].aux = fast[i].aux;
  }
  if (ok && !disk_fast.empty()) {
    ok = fwrite(&disk_fast[0], sizeof(CacheFastOp), disk_fast.size(), fp) ==
         disk_fast.size();
  }

  if (ok && !loop_terms.empty()) {
    ok = fwrite(&loop_terms[0], sizeof(LoopTerm), loop_terms.size(), fp) ==
         loop_terms.size();
  }

  fclose(fp);
  if (ok)
    rename(tmp_path.c_str(), cache_path.c_str());
  else
    remove(tmp_path.c_str());
}

int read_mem(const vector<byte>& mem, int index) {
  return mem[index-1] * 65536 + mem[index] * 256 + mem[index+1];
}

void dump_state(const vector<byte>& mem) {
  static const char* kRegs[] = {
    "PC", "A", "B", "C", "D", "BP", "SP"
  };
  for (int i = 0; i < 7; i++) {
    if (i)
      printf(" ");
    int v = read_mem(mem, 8 + 6 * i);
    if (i == 0)
      v--;
    printf("%s=%d", kRegs[i], v);
  }
  printf("\n");
  fflush(stdout);
}

void enable_raw_terminal() {
  if (!g_doom_host || !isatty(STDIN_FILENO))
    return;

  struct termios raw;
  if (tcgetattr(STDIN_FILENO, &g_old_termios) != 0)
    return;

  raw = g_old_termios;
  raw.c_lflag &= ~(ICANON | ECHO);
  raw.c_cc[VMIN] = 0;
  raw.c_cc[VTIME] = 0;

  if (tcsetattr(STDIN_FILENO, TCSANOW, &raw) == 0)
    g_raw_terminal = true;
}

void disable_raw_terminal() {
  if (g_raw_terminal) {
    tcsetattr(STDIN_FILENO, TCSANOW, &g_old_termios);
    g_raw_terminal = false;
  }
}

bool load_wad(const string& path) {
  ifstream fp(path.c_str(), ios::binary);
  if (!fp)
    return false;
  g_wad.assign(istreambuf_iterator<char>(fp), istreambuf_iterator<char>());
  g_wad_lumps.clear();
  g_wad_positions.clear();
  g_wad_sizes.clear();
  g_texture_names.clear();
  g_prepared_sprites.clear();
  if (g_wad.size() >= 12 && g_wad[0] == 'I' && g_wad[1] == 'W' &&
      g_wad[2] == 'A' && g_wad[3] == 'D') {
    int count = g_wad[4] | (g_wad[5] << 8) | (g_wad[6] << 16);
    int dir = g_wad[8] | (g_wad[9] << 8) | (g_wad[10] << 16);
    for (int i = 0; i < count && dir + i * 16 + 15 < (int)g_wad.size(); i++) {
      int entry = dir + i * 16;
      int pos = g_wad[entry] | (g_wad[entry + 1] << 8) |
                (g_wad[entry + 2] << 16);
      int size = g_wad[entry + 4] | (g_wad[entry + 5] << 8) |
                 (g_wad[entry + 6] << 16);
      string name;
      for (int j = 0; j < 8; j++) {
        byte c = g_wad[entry + 8 + j];
        if (c == 0)
          break;
        name.push_back(static_cast<char>(toupper(c)));
      }
      g_wad_lumps.push_back(name);
      g_wad_positions.push_back(pos);
      g_wad_sizes.push_back(size);
    }
  }
  return !g_wad.empty();
}

string lower_basename(string path) {
  size_t slash = path.find_last_of("/\\");
  if (slash != string::npos)
    path = path.substr(slash + 1);
  transform(path.begin(), path.end(), path.begin(), [](unsigned char c) {
    return static_cast<char>(tolower(c));
  });
  return path;
}

bool is_host_wad_path(const string& path) {
  string base = lower_basename(path);
  return base == "doom1.wad";
}

void enqueue_bfio_byte(byte value) {
  g_input_queue.push_back(((value >> 4) & 15) + 1);
  g_input_queue.push_back((value & 15) + 1);
}

void enqueue_u24(int value) {
  enqueue_bfio_byte(value & 255);
  enqueue_bfio_byte((value >> 8) & 255);
  enqueue_bfio_byte((value >> 16) & 255);
}

int read_host_stdin_byte() {
  if (!g_raw_terminal && isatty(STDIN_FILENO))
    return -1;

  fd_set rfds;
  FD_ZERO(&rfds);
  FD_SET(STDIN_FILENO, &rfds);
  struct timeval tv = {0, 0};

  if (select(STDIN_FILENO + 1, &rfds, NULL, NULL, &tv) <= 0)
    return -1;

  unsigned char ch;
  int n = read(STDIN_FILENO, &ch, 1);
  if (n == 0)
    return -2;
  if (n != 1)
    return -1;
  return ch;
}

byte map_key(int ch) {
  switch (ch) {
    case 'w':
    case 'W':
      return 0xad;
    case 's':
    case 'S':
      return 0xaf;
    case 'a':
    case 'A':
      return 0xac;
    case 'd':
    case 'D':
      return 0xae;
    case 'e':
    case 'E':
      return 0xa2;
    case ' ':
    case 'f':
    case 'F':
      return 0xa3;
    case '\r':
    case '\n':
      return 13;
    case 'q':
    case 'Q':
    case 27:
      return 27;
    default:
      if (0 < ch && ch < 128)
        return ch;
      return 0;
  }
}

pair<byte, byte> poll_key_event() {
  if (!g_key_events.empty()) {
    pair<byte, byte> ev = g_key_events.front();
    g_key_events.pop_front();
    return ev;
  }

  int ch = read_host_stdin_byte();
  if (ch < 0)
    return make_pair((byte)0, (byte)0);

  byte key = map_key(ch);
  if (!key)
    return make_pair((byte)0, (byte)0);

  g_key_events.push_back(make_pair((byte)2, key));
  return make_pair((byte)1, key);
}

void enqueue_key_poll() {
  pair<byte, byte> ev = poll_key_event();
  enqueue_bfio_byte(ev.first);
  enqueue_bfio_byte(ev.second);
}

string normalize_lump_name(const string& name) {
  string result;
  for (size_t i = 0; i < name.size() && i < 8 && name[i] != '\0'; i++) {
    result.push_back(static_cast<char>(toupper(static_cast<unsigned char>(name[i]))));
  }
  return result;
}

int find_lump_index(const string& name) {
  string key = normalize_lump_name(name);
  for (int i = (int)g_wad_lumps.size() - 1; i >= 0; i--) {
    if (g_wad_lumps[i] == key)
      return i;
  }
  return -1;
}

void save_first_frame_ppm(const vector<byte>& frame) {
  if (g_capture_path.empty())
    return;
  if (isatty(STDIN_FILENO) && g_frame_count != 1)
    return;

  FILE* fp = fopen(g_capture_path.c_str(), "wb");
  if (!fp)
    return;

  fprintf(fp, "P6\n%d %d\n255\n", kFrameWidth, kFrameHeight);
  fwrite(frame.data(), 1, frame.size(), fp);
  fclose(fp);
}

string snapshot_path() {
  return g_program_path + ".bfosnap";
}

bool source_mtime(unsigned long long* mtime) {
  struct stat st;
  if (g_program_path.empty() || stat(g_program_path.c_str(), &st) != 0)
    return false;
  *mtime = static_cast<unsigned long long>(st.st_mtime);
  return true;
}

bool load_vm_snapshot(vector<byte>* mem, int* mp, size_t* pc) {
  if (!g_doom_host || g_trace || g_program_path.empty())
    return false;

  unsigned long long current_mtime = 0;
  if (!source_mtime(&current_mtime))
    return false;

  FILE* fp = fopen(snapshot_path().c_str(), "rb");
  if (!fp)
    return false;

  unsigned int magic = 0;
  unsigned int version = 0;
  unsigned long long saved_mtime = 0;
  unsigned long long saved_pc = 0;
  int saved_mp = 0;
  unsigned long long mem_size = 0;
  unsigned long long render_arg_size = 0;
  unsigned long long draw_arg_size = 0;
  bool ok = fread(&magic, sizeof(magic), 1, fp) == 1 &&
            fread(&version, sizeof(version), 1, fp) == 1 &&
            fread(&saved_mtime, sizeof(saved_mtime), 1, fp) == 1 &&
            fread(&saved_pc, sizeof(saved_pc), 1, fp) == 1 &&
            fread(&saved_mp, sizeof(saved_mp), 1, fp) == 1 &&
            fread(&mem_size, sizeof(mem_size), 1, fp) == 1 &&
            fread(&render_arg_size, sizeof(render_arg_size), 1, fp) == 1 &&
            fread(&draw_arg_size, sizeof(draw_arg_size), 1, fp) == 1;
  if (!ok || magic != kSnapshotMagic || version != kSnapshotVersion ||
      saved_mtime != current_mtime || mem_size == 0 ||
      mem_size > 1024ULL * 1024ULL * 1024ULL ||
      render_arg_size > 256 || draw_arg_size > 256) {
    fclose(fp);
    return false;
  }

  mem->resize(static_cast<size_t>(mem_size));
  if (fread(&(*mem)[0], 1, mem->size(), fp) != mem->size()) {
    fclose(fp);
    return false;
  }
  g_last_render_args.resize(static_cast<size_t>(render_arg_size));
  g_last_draw_args.resize(static_cast<size_t>(draw_arg_size));
  if ((!g_last_render_args.empty() &&
       fread(&g_last_render_args[0], 1, g_last_render_args.size(), fp) !=
           g_last_render_args.size()) ||
      (!g_last_draw_args.empty() &&
       fread(&g_last_draw_args[0], 1, g_last_draw_args.size(), fp) !=
           g_last_draw_args.size())) {
    fclose(fp);
    return false;
  }
  fclose(fp);

  *mp = saved_mp;
  *pc = static_cast<size_t>(saved_pc);
  g_snapshot_loaded = true;
  return true;
}

void save_vm_snapshot(const vector<byte>& mem, int mp, size_t pc) {
  if (!g_doom_host || g_snapshot_saved || g_snapshot_loaded ||
      g_program_path.empty())
    return;

  unsigned long long current_mtime = 0;
  if (!source_mtime(&current_mtime))
    return;

  string path = snapshot_path();
  string tmp = path + ".tmp";
  FILE* fp = fopen(tmp.c_str(), "wb");
  if (!fp)
    return;

  unsigned int magic = kSnapshotMagic;
  unsigned int version = kSnapshotVersion;
  unsigned long long saved_pc = static_cast<unsigned long long>(pc);
  unsigned long long mem_size = static_cast<unsigned long long>(mem.size());
  unsigned long long render_arg_size =
      static_cast<unsigned long long>(g_last_render_args.size());
  unsigned long long draw_arg_size =
      static_cast<unsigned long long>(g_last_draw_args.size());
  bool ok = fwrite(&magic, sizeof(magic), 1, fp) == 1 &&
            fwrite(&version, sizeof(version), 1, fp) == 1 &&
            fwrite(&current_mtime, sizeof(current_mtime), 1, fp) == 1 &&
            fwrite(&saved_pc, sizeof(saved_pc), 1, fp) == 1 &&
            fwrite(&mp, sizeof(mp), 1, fp) == 1 &&
            fwrite(&mem_size, sizeof(mem_size), 1, fp) == 1 &&
            fwrite(&render_arg_size, sizeof(render_arg_size), 1, fp) == 1 &&
            fwrite(&draw_arg_size, sizeof(draw_arg_size), 1, fp) == 1 &&
            fwrite(&mem[0], 1, mem.size(), fp) == mem.size() &&
            (g_last_render_args.empty() ||
             fwrite(&g_last_render_args[0], 1, g_last_render_args.size(), fp) ==
                 g_last_render_args.size()) &&
            (g_last_draw_args.empty() ||
             fwrite(&g_last_draw_args[0], 1, g_last_draw_args.size(), fp) ==
                 g_last_draw_args.size());
  fclose(fp);

  if (ok) {
    rename(tmp.c_str(), path.c_str());
    g_snapshot_saved = true;
  } else {
    remove(tmp.c_str());
  }
}

void render_frame_terminal(const vector<byte>& frame) {
  int cols = 180;
  int rows = 56;
  int term_cols = cols;

  if (!isatty(STDERR_FILENO)) {
    fprintf(stderr,
            "frame %d  keys: WASD/arrows move/turn, Space/F fire, 1-7 weapon, E use, Q/Esc menu\n",
            g_frame_count);
    fflush(stderr);
    return;
  }

  struct winsize ws;
  if (ioctl(STDERR_FILENO, TIOCGWINSZ, &ws) == 0) {
    if (ws.ws_col > 0) {
      cols = ws.ws_col;
      term_cols = ws.ws_col;
    }
    if (ws.ws_row > 2)
      rows = ws.ws_row - 2;
  }
  if (cols > 180)
    cols = 180;
  if (cols < 20)
    cols = min(20, term_cols);

  int max_rows = min(rows, 50);
  if (max_rows < 8)
    max_rows = 8;
  rows = cols * kFrameHeight / (kFrameWidth * 2);
  if (rows < 8)
    rows = 8;
  if (rows > max_rows) {
    rows = max_rows;
    cols = rows * kFrameWidth * 2 / kFrameHeight;
    if (cols > term_cols)
      cols = term_cols;
  }
  if (cols > 180)
    cols = 180;
  if (cols < 20)
    cols = 20;

  if (!g_terminal_started) {
    fprintf(stderr, "\033[2J\033[?25l\033[?7l");
    g_terminal_started = true;
  }

  fprintf(stderr, "\033[H");

  for (int y = 0; y < rows; y++) {
    int sy_top = (y * 2) * kFrameHeight / (rows * 2);
    int sy_bottom = (y * 2 + 1) * kFrameHeight / (rows * 2);
    for (int x = 0; x < cols; x++) {
      int sx = x * kFrameWidth / cols;
      int top = (sy_top * kFrameWidth + sx) * 3;
      int bottom = (sy_bottom * kFrameWidth + sx) * 3;
      fprintf(stderr,
              "\033[38;2;%d;%d;%dm\033[48;2;%d;%d;%dm▀",
              frame[top], frame[top + 1], frame[top + 2], frame[bottom],
              frame[bottom + 1], frame[bottom + 2]);
    }
    fprintf(stderr, "\033[0m\n");
  }

  char status[160];
  snprintf(status, sizeof(status),
           "frame %d  keys: WASD/arrows move/turn, Space/F fire, 1-7 weapon, E use, Q/Esc menu",
           g_frame_count);
  status[max(0, min(cols, (int)sizeof(status) - 1))] = '\0';
  fprintf(stderr, "\033[0m%s\033[K\n\033[J", status);
  fflush(stderr);
}

void write_u16_le(FILE* fp, unsigned int value) {
  fputc(value & 255, fp);
  fputc((value >> 8) & 255, fp);
}

void write_u32_le(FILE* fp, unsigned int value) {
  fputc(value & 255, fp);
  fputc((value >> 8) & 255, fp);
  fputc((value >> 16) & 255, fp);
  fputc((value >> 24) & 255, fp);
}

void stream_frame_window(const vector<byte>& frame) {
  fwrite("BFDW", 1, 4, stdout);
  write_u16_le(stdout, kFrameWidth);
  write_u16_le(stdout, kFrameHeight);
  write_u32_le(stdout, static_cast<unsigned int>(g_frame_count));
  write_u32_le(stdout, static_cast<unsigned int>(frame.size()));
  fwrite(frame.data(), 1, frame.size(), stdout);
  fflush(stdout);
}

void handle_frame(const vector<byte>& frame) {
  g_frame_count++;
  save_first_frame_ppm(frame);
  if (g_window_stream)
    stream_frame_window(frame);
  else
    render_frame_terminal(frame);
  if (g_frame_count == 1)
    g_snapshot_due = true;
}

void enqueue_read_response(int offset, int len) {
  if (offset < 0)
    offset = 0;
  if (len < 0)
    len = 0;
  for (int i = 0; i < len; i++) {
    int p = offset + i;
    g_input_queue.push_back(0 <= p && p < (int)g_wad.size() ? g_wad[p] : 0);
  }
}

int elvm_word_cell(int addr) {
  int hi = addr / 256;
  int lo = addr % 256;
  return kElvmMem + kElvmMemBlkLen * hi + kElvmMemCtlLen + lo * 3;
}

void write_elvm_word(int addr, int value) {
  if (!g_active_mem || addr < 0)
    return;

  int cell = elvm_word_cell(addr);
  if (cell + 1 >= (int)g_active_mem->size()) {
    g_active_mem->resize(max(g_active_mem->size() * 2,
                             static_cast<size_t>(cell + 2)));
  }

  (*g_active_mem)[cell - 1] = (value >> 16) & 255;
  (*g_active_mem)[cell] = (value >> 8) & 255;
  (*g_active_mem)[cell + 1] = value & 255;
}

int read_elvm_word_raw(int addr) {
  if (!g_active_mem || addr < 0)
    return 0;

  int cell = elvm_word_cell(addr);
  if (cell + 1 >= (int)g_active_mem->size())
    return 0;

  return ((*g_active_mem)[cell - 1] << 16) |
         ((*g_active_mem)[cell] << 8) |
         (*g_active_mem)[cell + 1];
}

int sign24(int value) {
  value &= 0xffffff;
  return value & 0x800000 ? value - 0x1000000 : value;
}

int read_elvm_word(int addr) {
  return sign24(read_elvm_word_raw(addr));
}

string read_elvm_name(int addr, int max_len) {
  string name;
  for (int i = 0; i < max_len; i++) {
    int c = read_elvm_word_raw(addr + i) & 255;
    if (c == 0)
      break;
    name.push_back(static_cast<char>(toupper(c)));
  }
  return name;
}

string read_wad_name(int addr, int max_len) {
  string name;
  for (int i = 0; i < max_len; i++) {
    int p = addr + i;
    if (p < 0 || p >= (int)g_wad.size() || g_wad[p] == 0)
      break;
    name.push_back(static_cast<char>(toupper(g_wad[p])));
  }
  return name;
}

void write_elvm_byte(int addr, byte value) {
  write_elvm_word(addr, value);
}

int read_u24_arg(const vector<byte>& args, int index) {
  int off = index * 3;
  return args[off] | (args[off + 1] << 8) | (args[off + 2] << 16);
}

int wad_le24(int offset) {
  if (offset < 0 || offset + 2 >= (int)g_wad.size())
    return 0;
  return g_wad[offset] | (g_wad[offset + 1] << 8) | (g_wad[offset + 2] << 16);
}

int wad_le32(int offset) {
  if (offset < 0 || offset + 3 >= (int)g_wad.size())
    return 0;
  return g_wad[offset] | (g_wad[offset + 1] << 8) |
         (g_wad[offset + 2] << 16) | (g_wad[offset + 3] << 24);
}

int wad_le16s(int offset) {
  if (offset < 0 || offset + 1 >= (int)g_wad.size())
    return 0;

  int value = g_wad[offset] | (g_wad[offset + 1] << 8);
  return value >= 32768 ? value - 65536 : value;
}

int fixed_from_i16(int value) {
  return value * 65536;
}

bool host_actor_is_enemy(int type) {
  switch (type) {
    case 9:
    case 58:
    case 3001:
    case 3002:
    case 3003:
    case 3004:
    case 3005:
    case 3006:
      return true;
    default:
      return false;
  }
}

int host_actor_start_health(int type) {
  switch (type) {
    case 9:
      return 30;
    case 3004:
      return 20;
    case 3001:
      return 60;
    case 58:
    case 3002:
      return 150;
    case 3005:
      return 400;
    case 3006:
      return 100;
    case 3003:
      return 1000;
    default:
      return host_actor_is_enemy(type) ? 20 : 1;
  }
}

double host_actor_radius(int type) {
  switch (type) {
    case 9:
    case 3001:
    case 3004:
      return 20.0;
    case 58:
    case 3002:
      return 30.0;
    case 3005:
      return 31.0;
    case 3003:
      return 24.0;
    case 3006:
      return 16.0;
    default:
      return host_actor_is_enemy(type) ? 20.0 : 8.0;
  }
}

bool host_actor_is_weapon(int type) {
  return 2001 <= type && type <= 2006;
}

int host_weapon_slot_from_actor(int type) {
  switch (type) {
    case 2001:
      return 3;
    case 2002:
      return 4;
    case 2003:
      return 5;
    case 2004:
      return 6;
    case 2005:
      return 1;
    case 2006:
      return 7;
    default:
      return 0;
  }
}

const char* host_weapon_patch_name(bool firing) {
  switch (g_host_weapon) {
    case 1:
      return firing ? "SAWGB0" : "SAWGA0";
    case 3:
      return firing ? "SHTFA0" : "SHTGA0";
    case 4:
      return firing ? "CHGFA0" : "CHGGA0";
    case 5:
      return firing ? "MISGB0" : "MISGA0";
    default:
      return firing ? "PISFA0" : "PISGA0";
  }
}

int host_p_random() {
  g_host_prndindex = (g_host_prndindex + 1) & 0xff;
  return kDoomRndTable[g_host_prndindex];
}

int host_bullet_damage() {
  return 5 * (host_p_random() % 3 + 1);
}

int host_melee_damage() {
  switch (g_host_weapon) {
    case 1:
      return 2 * (host_p_random() % 10 + 1);
    default:
      return (host_p_random() % 10 + 1) << 1;
  }
}

double host_doom_angle_spread(int shift) {
  int delta = host_p_random() - host_p_random();
  double fixed_delta = static_cast<double>(delta) * static_cast<double>(1 << shift);
  return fixed_delta * (2.0 * kPi / 4294967296.0);
}

int host_weapon_damage() {
  switch (g_host_weapon) {
    case 1:
      return host_melee_damage();
    case 5:
      return 100;
    default:
      return host_bullet_damage();
  }
}

int host_weapon_ammo_cost() {
  if (g_host_weapon == 1)
    return 0;
  if (g_host_weapon == 5)
    return 1;
  if (g_host_weapon == 7)
    return 40;
  return 1;
}

int host_weapon_ammo_type() {
  switch (g_host_weapon) {
    case 1:
      return -1;
    case 3:
      return HOST_AMMO_SHELL;
    case 5:
      return HOST_AMMO_ROCKET;
    case 6:
    case 7:
      return HOST_AMMO_CELL;
    default:
      return HOST_AMMO_BULLET;
  }
}

int host_current_ammo() {
  int ammo_type = host_weapon_ammo_type();
  return ammo_type < 0 ? 0 : g_host_ammo[ammo_type];
}

void host_add_ammo(int ammo_type, int amount) {
  if (ammo_type < 0 || ammo_type >= HOST_AMMO_COUNT)
    return;
  g_host_ammo[ammo_type] += amount;
  if (g_host_ammo[ammo_type] > g_host_ammo_max[ammo_type])
    g_host_ammo[ammo_type] = g_host_ammo_max[ammo_type];
}

void host_add_weapon_ammo(int type) {
  switch (type) {
    case 2001:
      host_add_ammo(HOST_AMMO_SHELL, 8);
      break;
    case 2002:
      host_add_ammo(HOST_AMMO_BULLET, 20);
      break;
    case 2003:
      host_add_ammo(HOST_AMMO_ROCKET, 2);
      break;
    case 2004:
    case 2006:
      host_add_ammo(HOST_AMMO_CELL, 40);
      break;
    default:
      break;
  }
}

void host_add_pickup_ammo(int type) {
  switch (type) {
    case 2007:
      host_add_ammo(HOST_AMMO_BULLET, 10);
      break;
    case 2048:
      host_add_ammo(HOST_AMMO_BULLET, 50);
      break;
    case 2008:
      host_add_ammo(HOST_AMMO_SHELL, 4);
      break;
    case 2049:
      host_add_ammo(HOST_AMMO_SHELL, 20);
      break;
    case 2010:
      host_add_ammo(HOST_AMMO_ROCKET, 1);
      break;
    case 2047:
      host_add_ammo(HOST_AMMO_ROCKET, 5);
      break;
    case 17:
    case 2046:
      host_add_ammo(HOST_AMMO_CELL, type == 17 ? 100 : 20);
      break;
    default:
      break;
  }
}

double host_weapon_aim_width(double dist) {
  double base = 24.0 / (dist + 1.0);
  if (g_host_weapon == 3)
    base *= 2.2;
  if (g_host_weapon == 1)
    base *= 1.6;
  if (base < 0.055)
    base = 0.055;
  if (base > 0.22)
    base = 0.22;
  return base;
}

bool host_actor_is_ammo(int type) {
  switch (type) {
    case 17:
    case 2007:
    case 2008:
    case 2010:
    case 2046:
    case 2047:
    case 2048:
    case 2049:
      return true;
    default:
      return false;
  }
}

bool host_actor_is_health(int type) {
  switch (type) {
    case 2011:
    case 2012:
    case 2013:
    case 2014:
      return true;
    default:
      return false;
  }
}

bool host_actor_is_armor(int type) {
  return type == 2015 || type == 2018 || type == 2019;
}

int host_actor_color(int type) {
  if (host_actor_is_enemy(type))
    return type == 3001 ? 72 : type == 3002 ? 64 : type == 3004 ? 48 : 56;
  if (host_actor_is_weapon(type) || host_actor_is_ammo(type))
    return 180;
  if (host_actor_is_health(type))
    return 32;
  if (host_actor_is_armor(type))
    return 112;
  return 160;
}

bool host_actor_should_render(int type) {
  if (host_actor_is_enemy(type))
    return true;
  switch (type) {
    case 2001:
    case 2002:
    case 2003:
    case 2004:
    case 2005:
    case 2006:
    case 2007:
    case 2008:
    case 2010:
    case 2011:
    case 2012:
    case 2013:
    case 2014:
    case 2015:
    case 2018:
    case 2019:
    case 2045:
    case 2046:
    case 2047:
    case 2048:
    case 2049:
      return true;
    default:
      return false;
  }
}

bool host_mapthing_allowed_for_medium_single(int options) {
  const int kNotSinglePlayer = 16;
  const int kMediumSkill = 2;
  if (options & kNotSinglePlayer)
    return false;
  return (options & kMediumSkill) != 0;
}

void host_apply_damage(int damage) {
  if (damage <= 0 || g_host_health <= 0)
    return;

  int absorbed = damage / 3;
  if (absorbed > g_host_armor)
    absorbed = g_host_armor;
  g_host_armor -= absorbed;
  damage -= absorbed;
  g_host_health -= damage;
  if (g_host_health < 0)
    g_host_health = 0;
  g_host_damage_flash = 4;
  if (!isatty(STDERR_FILENO))
    fprintf(stderr, "damage health=%d armor=%d\n", g_host_health, g_host_armor);
}

void ensure_host_actors() {
  if (g_host_actors_ready)
    return;
  g_host_actors_ready = true;
  g_host_actors.clear();
  g_host_actor_spawned = 0;
  g_host_actor_skipped_skill = 0;
  g_host_actor_skipped_single = 0;
  g_host_actor_ambush = 0;

  int map = find_lump_index(g_host_map_name);
  if (map < 0 || map + 1 >= (int)g_wad_lumps.size() ||
      g_wad_lumps[map + 1] != "THINGS")
    return;

  int pos = g_wad_positions[map + 1];
  int count = g_wad_sizes[map + 1] / 10;
  for (int i = 0; i < count; i++) {
    int rec = pos + i * 10;
    int type = wad_le16s(rec + 6);
    int options = wad_le16s(rec + 8);
    if (type <= 4 || type == 11)
      continue;
    if (!host_actor_should_render(type))
      continue;
    if (options & 16) {
      g_host_actor_skipped_single++;
      continue;
    }
    if (!host_mapthing_allowed_for_medium_single(options)) {
      g_host_actor_skipped_skill++;
      continue;
    }
    HostActor actor;
    actor.x = wad_le16s(rec);
    actor.y = wad_le16s(rec + 2);
    actor.angle = wad_le16s(rec + 4);
    actor.type = type;
    actor.options = options;
    actor.health = host_actor_start_health(type);
    actor.flash = 0;
    actor.attack_tics = 0;
    actor.death_tics = 0;
    actor.ambush = (options & 8) != 0;
    actor.awake = false;
    actor.alive = true;
    g_host_actors.push_back(actor);
    g_host_actor_spawned++;
    if (actor.ambush)
      g_host_actor_ambush++;
  }
  if (!isatty(STDERR_FILENO)) {
    fprintf(stderr, "actors spawned=%d skipped_skill=%d skipped_single=%d ambush=%d\n",
            g_host_actor_spawned, g_host_actor_skipped_skill,
            g_host_actor_skipped_single, g_host_actor_ambush);
  }
}

double normalize_host_angle(double angle) {
  while (angle < -kPi)
    angle += 2.0 * kPi;
  while (angle > kPi)
    angle -= 2.0 * kPi;
  return angle;
}

string host_texture_name_at(int offset) {
  string name = read_wad_name(offset, 8);
  return name.empty() ? "-" : name;
}

int host_texture_color_base(const string& name) {
  if (name.find("COM") != string::npos || name.find("COMP") != string::npos ||
      name.find("LITE") != string::npos)
    return 96;
  if (name.find("BROWN") != string::npos || name.find("WOOD") != string::npos ||
      name.find("STARTAN") != string::npos)
    return 64;
  if (name.find("STONE") != string::npos || name.find("GRAY") != string::npos ||
      name.find("SUPPORT") != string::npos || name.find("DOOR") != string::npos)
    return 96;
  if (name.find("BRICK") != string::npos || name.find("RED") != string::npos)
    return 32;
  if (name.find("METAL") != string::npos || name.find("PIPE") != string::npos)
    return 88;
  if (name.find("NUK") != string::npos)
    return 116;
  return 72;
}

bool host_line_special_is_door(int special) {
  switch (special) {
    case 1:
    case 26:
    case 27:
    case 28:
    case 31:
    case 32:
    case 33:
    case 34:
    case 63:
    case 114:
    case 117:
      return true;
    default:
      return false;
  }
}

bool host_line_special_is_exit(int special) {
  switch (special) {
    case 11:
    case 51:
    case 52:
    case 124:
    case 197:
      return true;
    default:
      return false;
  }
}

void ensure_host_map() {
  if (g_host_map_ready)
    return;
  g_host_map_ready = true;
  g_host_lines.clear();
  g_host_has_sky = false;

  int map = find_lump_index(g_host_map_name);
  if (map < 0 || map + 4 >= (int)g_wad_lumps.size())
    return;

  int line_pos = g_wad_positions[map + 2];
  int line_count = g_wad_sizes[map + 2] / 14;
  int side_pos = g_wad_positions[map + 3];
  int side_count = g_wad_sizes[map + 3] / 30;
  int vertex_pos = g_wad_positions[map + 4];
  int vertex_count = g_wad_sizes[map + 4] / 4;
  int sector_pos = g_wad_positions[map + 8];
  int sector_count = g_wad_sizes[map + 8] / 26;
  if (sector_count > 0) {
    g_host_floor_flat = host_texture_name_at(sector_pos + 4);
    g_host_ceiling_flat = host_texture_name_at(sector_pos + 12);
  }
  for (int i = 0; i < sector_count; i++) {
    string ceiling = host_texture_name_at(sector_pos + i * 26 + 12);
    if (ceiling == "F_SKY1")
      g_host_has_sky = true;
  }

  for (int i = 0; i < line_count; i++) {
    int rec = line_pos + i * 14;
    int v1 = g_wad[rec] | (g_wad[rec + 1] << 8);
    int v2 = g_wad[rec + 2] | (g_wad[rec + 3] << 8);
    int flags = g_wad[rec + 4] | (g_wad[rec + 5] << 8);
    int special = g_wad[rec + 6] | (g_wad[rec + 7] << 8);
    int tag = g_wad[rec + 8] | (g_wad[rec + 9] << 8);
    int side0 = g_wad[rec + 10] | (g_wad[rec + 11] << 8);
    int side1 = g_wad[rec + 12] | (g_wad[rec + 13] << 8);
    if (v1 < 0 || v1 >= vertex_count || v2 < 0 || v2 >= vertex_count)
      continue;

    int r1 = vertex_pos + v1 * 4;
    int r2 = vertex_pos + v2 * 4;
    string top = "-";
    string bottom = "-";
    string mid = "-";
    int front_sector = -1;
    int back_sector = -1;
    int front_floor = 0;
    int front_ceiling = 128;
    int back_floor = 0;
    int back_ceiling = 0;
    int light = 192;
    if (side0 >= 0 && side0 < side_count) {
      int side = side_pos + side0 * 30;
      top = host_texture_name_at(side + 4);
      bottom = host_texture_name_at(side + 12);
      mid = host_texture_name_at(side + 20);
      front_sector = g_wad[side + 28] | (g_wad[side + 29] << 8);
      if (front_sector >= 0 && front_sector < sector_count) {
        int sector = sector_pos + front_sector * 26;
        front_floor = wad_le16s(sector);
        front_ceiling = wad_le16s(sector + 2);
        light = wad_le16s(sector + 20);
      }
    }
    if (side1 >= 0 && side1 < side_count) {
      int side = side_pos + side1 * 30;
      back_sector = g_wad[side + 28] | (g_wad[side + 29] << 8);
      if (back_sector >= 0 && back_sector < sector_count) {
        int sector = sector_pos + back_sector * 26;
        back_floor = wad_le16s(sector);
        back_ceiling = wad_le16s(sector + 2);
      }
    }

    HostLine line;
    line.x1 = wad_le16s(r1);
    line.y1 = wad_le16s(r1 + 2);
    line.x2 = wad_le16s(r2);
    line.y2 = wad_le16s(r2 + 2);
    string texture = mid != "-" ? mid : (top != "-" ? top : bottom);
    line.texture = texture;
    line.color_base = host_texture_color_base(texture);
    line.light = light;
    line.flags = flags;
    line.front_floor = front_floor;
    line.front_ceiling = front_ceiling;
    line.back_floor = back_floor;
    line.back_ceiling = back_ceiling;
    line.special = special;
    line.tag = tag;
    bool one_sided = side1 == 65535 || front_sector < 0 || back_sector < 0;
    int opening_height = std::min(front_ceiling, back_ceiling) -
                         std::max(front_floor, back_floor);
    int floor_step = std::abs(back_floor - front_floor);
    bool geometry_solid = one_sided || opening_height < 56 || floor_step > 24;
    line.solid = geometry_solid;
    line.blocks_move = geometry_solid || (flags & 1);
    line.door = texture.find("DOOR") != string::npos ||
                host_line_special_is_door(special);
    line.exit = texture.find("EXIT") != string::npos ||
                host_line_special_is_exit(special);
    g_host_lines.push_back(line);
  }
}

void ensure_host_player_start() {
  if (g_host_player_ready)
    return;
  g_host_player_ready = true;
  g_host_player_x = 1056.0;
  g_host_player_y = -3616.0;
  g_host_player_angle = kPi / 2.0;

  int map = find_lump_index(g_host_map_name);
  if (map < 0 || map + 1 >= (int)g_wad_lumps.size())
    return;

  int pos = g_wad_positions[map + 1];
  int count = g_wad_sizes[map + 1] / 10;
  for (int i = 0; i < count; i++) {
    int rec = pos + i * 10;
    if (wad_le16s(rec + 6) != 1)
      continue;
    g_host_player_x = wad_le16s(rec);
    g_host_player_y = wad_le16s(rec + 2);
    g_host_player_angle = wad_le16s(rec + 4) * (2.0 * kPi / 360.0);
    return;
  }
}

bool host_set_map(int episode, int map_number) {
  char name[8];
  snprintf(name, sizeof(name), "E%dM%d", episode, map_number);
  if (find_lump_index(name) < 0)
    return false;

  g_host_episode = episode;
  g_host_map = map_number;
  g_host_map_name = name;
  g_host_map_ready = false;
  g_host_actors_ready = false;
  g_host_player_ready = false;
  g_host_has_sky = false;
  g_host_lines.clear();
  g_host_actors.clear();
  g_host_level_complete = false;
  return true;
}

bool host_advance_map() {
  int next_map = g_host_map + 1;
  int next_episode = g_host_episode;
  if (next_map > 9) {
    next_episode++;
    next_map = 1;
  }

  if (host_set_map(next_episode, next_map))
    return true;
  return host_set_map(g_host_episode, 1);
}

bool host_path_blocked_by_line_map(double ax, double ay, double bx, double by);
bool host_line_blocked_by_map(double ax, double ay, double bx, double by);

double host_actor_angle_rad(const HostActor& actor) {
  return actor.angle * (2.0 * kPi / 360.0);
}

bool host_actor_front_sight(const HostActor& actor, double dx, double dy,
                            double dist, bool line_of_sight) {
  if (!line_of_sight)
    return false;
  if (dist <= 64.0)
    return true;
  double rel = normalize_host_angle(atan2(dy, dx) - host_actor_angle_rad(actor));
  return fabs(rel) <= kPi / 2.0;
}

void host_wake_actor(HostActor& actor) {
  actor.awake = true;
  actor.ambush = false;
}

void host_update_enemies() {
  ensure_host_actors();
  ensure_host_player_start();
  g_host_tick++;
  int sight_wakes = 0;
  for (size_t i = 0; i < g_host_actors.size(); i++) {
    HostActor& actor = g_host_actors[i];
    if (!host_actor_is_enemy(actor.type))
      continue;
    if (actor.attack_tics > 0)
      actor.attack_tics--;
    if (!actor.alive) {
      if (actor.death_tics > 0 && actor.death_tics < 40)
        actor.death_tics++;
      continue;
    }

    double dx = g_host_player_x - actor.x;
    double dy = g_host_player_y - actor.y;
    double dist = sqrt(dx * dx + dy * dy);
    if (dist < 1.0 || dist > 900.0)
      continue;

    bool line_of_sight = !host_line_blocked_by_map(actor.x, actor.y,
                                                   g_host_player_x,
                                                   g_host_player_y);
    if (!actor.awake) {
      if (!host_actor_front_sight(actor, dx, dy, dist, line_of_sight))
        continue;
      host_wake_actor(actor);
      sight_wakes++;
    }
    if (dist < 48.0 && line_of_sight) {
      if ((g_host_tick % 10) == 0) {
        actor.attack_tics = 24;
        host_apply_damage(2);
      }
      continue;
    }

    actor.angle = static_cast<int>(atan2(dy, dx) * 360.0 / (2.0 * kPi));
    if (actor.angle < 0)
      actor.angle += 360;
    if (dist < 700.0 && line_of_sight &&
        (g_host_tick + static_cast<int>(i) * 7) % 45 == 0) {
      actor.attack_tics = 24;
      int damage = actor.type == 3001 ? 5 : 3;
      host_apply_damage(damage);
    }

    double step = 1.2;
    double nx = actor.x + dx / dist * step;
    double ny = actor.y + dy / dist * step;
    if (!host_path_blocked_by_line_map(actor.x, actor.y, nx, ny)) {
      actor.x = nx;
      actor.y = ny;
    }
  }
  if (sight_wakes > 0 && !isatty(STDERR_FILENO))
    fprintf(stderr, "wake sight=%d\n", sight_wakes);
}

void append_texture_names_from_lump(const char* lump_name) {
  int lump = find_lump_index(lump_name);
  if (lump < 0 || lump >= (int)g_wad_positions.size())
    return;

  int pos = g_wad_positions[lump];
  int count = wad_le32(pos);

  for (int i = 0; i < count; i++) {
    int offset = wad_le32(pos + 4 + i * 4);
    int name_pos = pos + offset;
    string name;

    for (int j = 0; j < 8; j++) {
      int p = name_pos + j;
      if (p < 0 || p >= (int)g_wad.size() || g_wad[p] == 0)
        break;
      name.push_back(static_cast<char>(toupper(g_wad[p])));
    }

    g_texture_names.push_back(name);
  }
}

void ensure_texture_names() {
  if (!g_texture_names.empty())
    return;

  append_texture_names_from_lump("TEXTURE1");
  append_texture_names_from_lump("TEXTURE2");
}

int find_texture_index(const string& name) {
  string key = normalize_lump_name(name);
  if (!key.empty() && key[0] == '-')
    return 0;

  ensure_texture_names();

  for (int i = 0; i < (int)g_texture_names.size(); i++) {
    if (g_texture_names[i] == key)
      return i;
  }

  return -1;
}

void process_bfio_dma(const vector<byte>& args) {
  int offset = read_u24_arg(args, 0);
  int len = read_u24_arg(args, 1);
  int dest = read_u24_arg(args, 2);

  if (offset < 0)
    offset = 0;
  if (len < 0)
    len = 0;

  for (int i = 0; i < len; i++) {
    int p = offset + i;
    write_elvm_byte(dest + i, 0 <= p && p < (int)g_wad.size() ? g_wad[p] : 0);
  }
}

void process_bfio_lumpdir(const vector<byte>& args) {
  int dir_offset = read_u24_arg(args, 0);
  int count = read_u24_arg(args, 1);
  int dest = read_u24_arg(args, 2);
  int wad_file = read_u24_arg(args, 3);
  int stride = read_u24_arg(args, 4);
  int wad_file_off = read_u24_arg(args, 5);
  int position_off = read_u24_arg(args, 6);
  int size_off = read_u24_arg(args, 7);
  int cache_off = read_u24_arg(args, 8);
  int next_off = read_u24_arg(args, 9);

  for (int i = 0; i < count; i++) {
    int entry = dir_offset + i * 16;
    int lump = dest + i * stride;

    for (int j = 0; j < 8; j++) {
      int p = entry + 8 + j;
      write_elvm_byte(lump + j, 0 <= p && p < (int)g_wad.size() ? g_wad[p] : 0);
    }

    write_elvm_word(lump + wad_file_off, wad_file);
    write_elvm_word(lump + position_off, wad_le24(entry));
    write_elvm_word(lump + size_off, wad_le24(entry + 4));
    write_elvm_word(lump + cache_off, 0);
    write_elvm_word(lump + next_off, 0);
  }
}

void process_bfio_patch_headers(const vector<byte>& args) {
  int first = read_u24_arg(args, 0);
  int count = read_u24_arg(args, 1);
  int width = read_u24_arg(args, 2);
  int left = read_u24_arg(args, 3);
  int top = read_u24_arg(args, 4);

  for (int i = 0; i < count; i++) {
    int lump = first + i;
    int pos = 0;

    if (0 <= lump && lump < (int)g_wad_positions.size())
      pos = g_wad_positions[lump];

    write_elvm_word(width + i, fixed_from_i16(wad_le16s(pos)));
    write_elvm_word(left + i, fixed_from_i16(wad_le16s(pos + 4)));
    write_elvm_word(top + i, fixed_from_i16(wad_le16s(pos + 6)));
  }
}

void process_bfio_patch_lookup(const vector<byte>& args) {
  int count = read_u24_arg(args, 0);
  int dest = read_u24_arg(args, 1);
  int pnames = find_lump_index("PNAMES");
  int fallback = 0;
  bool have_fallback = false;

  if (pnames < 0 || pnames >= (int)g_wad_positions.size())
    return;

  int pos = g_wad_positions[pnames];
  int available = wad_le24(pos);
  if (available < count)
    count = available;

  for (int i = 0; i < count; i++) {
    string name;
    int name_pos = pos + 4 + i * 8;
    for (int j = 0; j < 8; j++) {
      int p = name_pos + j;
      if (p < 0 || p >= (int)g_wad.size() || g_wad[p] == 0)
        break;
      name.push_back(static_cast<char>(toupper(g_wad[p])));
    }

    int lump = find_lump_index(name);
    if (lump >= 0) {
      fallback = lump;
      have_fallback = true;
      write_elvm_word(dest + i, lump);
    } else {
      write_elvm_word(dest + i, have_fallback ? fallback : 0);
    }
  }
}

void process_bfio_texture_lookup(const vector<byte>& args) {
  int count = read_u24_arg(args, 0);
  int textures = read_u24_arg(args, 1);
  int columnlump = read_u24_arg(args, 2);
  int columnofs = read_u24_arg(args, 3);
  int texturecomposite = read_u24_arg(args, 4);
  int compositesize = read_u24_arg(args, 5);
  int width_off = read_u24_arg(args, 6);
  int height_off = read_u24_arg(args, 7);
  int patchcount_off = read_u24_arg(args, 8);
  int patches_off = read_u24_arg(args, 9);
  int originx_off = read_u24_arg(args, 10);
  int patch_off = read_u24_arg(args, 11);
  int patch_stride = read_u24_arg(args, 12);

  for (int i = 0; i < count; i++) {
    int texture = read_elvm_word_raw(textures + i);
    int collump = read_elvm_word_raw(columnlump + i);
    int colofs = read_elvm_word_raw(columnofs + i);
    int width = read_elvm_word(texture + width_off);
    int height = read_elvm_word(texture + height_off);
    int patchcount = read_elvm_word(texture + patchcount_off);
    int size = 0;

    write_elvm_word(texturecomposite + i, 0);
    write_elvm_word(compositesize + i, 0);

    if (texture <= 0 || collump <= 0 || colofs <= 0 || width <= 0 ||
        patchcount <= 0) {
      continue;
    }
    if (width > 4096 || height > 4096 || patchcount > 1024) {
      fprintf(stderr,
              "[hostlookup bad texture %d ptr=%d width=%d height=%d patches=%d]",
              i, texture, width, height, patchcount);
      continue;
    }

    vector<byte> patch_counts(width, 0);

    for (int j = 0; j < patchcount; j++) {
      int patch = texture + patches_off + j * patch_stride;
      int originx = read_elvm_word(patch + originx_off);
      int lump = read_elvm_word(patch + patch_off);

      if (lump < 0 || lump >= (int)g_wad_positions.size())
        continue;

      int patch_pos = g_wad_positions[lump];
      int patch_width = wad_le16s(patch_pos);
      int x1 = originx;
      int x2 = originx + patch_width;
      int x = x1 < 0 ? 0 : x1;

      if (x2 > width)
        x2 = width;

      for (; x < x2; x++) {
        patch_counts[x]++;
        write_elvm_word(collump + x, lump);
        write_elvm_word(colofs + x, wad_le32(patch_pos + 8 + (x - x1) * 4) + 3);
      }
    }

    for (int x = 0; x < width; x++) {
      if (patch_counts[x] > 1) {
        write_elvm_word(collump + x, -1);
        write_elvm_word(colofs + x, size);
        size += height;
      }
    }

    write_elvm_word(compositesize + i, size);
  }
}

vector<int> load_patch_lookup_from_wad() {
  vector<int> lookup;
  int pnames = find_lump_index("PNAMES");
  if (pnames < 0 || pnames >= (int)g_wad_positions.size())
    return lookup;

  int pos = g_wad_positions[pnames];
  int count = wad_le32(pos);
  lookup.resize(count, 0);

  int fallback = 0;
  bool have_fallback = false;
  for (int i = 0; i < count; i++) {
    string name;
    int name_pos = pos + 4 + i * 8;
    for (int j = 0; j < 8; j++) {
      int p = name_pos + j;
      if (p < 0 || p >= (int)g_wad.size() || g_wad[p] == 0)
        break;
      name.push_back(static_cast<char>(toupper(g_wad[p])));
    }

    int lump = find_lump_index(name);
    if (lump >= 0) {
      fallback = lump;
      have_fallback = true;
      lookup[i] = lump;
    } else {
      lookup[i] = have_fallback ? fallback : 0;
    }
  }
  return lookup;
}

bool host_find_texture_record(const string& texture_name, int* record) {
  string key = normalize_lump_name(texture_name);
  if (key.empty() || key == "-")
    return false;

  const char* lumps[2] = {"TEXTURE1", "TEXTURE2"};
  for (int li = 0; li < 2; li++) {
    int lump = find_lump_index(lumps[li]);
    if (lump < 0 || lump >= (int)g_wad_positions.size())
      continue;

    int pos = g_wad_positions[lump];
    int size = g_wad_sizes[lump];
    if (size < 4 || pos < 0 || pos + size > (int)g_wad.size())
      continue;

    int count = wad_le32(pos);
    if (count <= 0 || 4 + count * 4 > size)
      continue;

    for (int i = 0; i < count; i++) {
      int rec = pos + wad_le32(pos + 4 + i * 4);
      if (rec < pos || rec + 22 > pos + size)
        continue;
      if (normalize_lump_name(read_wad_name(rec, 8)) == key) {
        *record = rec;
        return true;
      }
    }
  }

  return false;
}

void host_blit_patch_to_texture(HostTexture* texture, int lump,
                                int originx, int originy) {
  if (lump < 0 || lump >= (int)g_wad_positions.size())
    return;

  int pos = g_wad_positions[lump];
  int size = g_wad_sizes[lump];
  if (size < 8 || pos < 0 || pos + size > (int)g_wad.size())
    return;

  int patch_width = g_wad[pos] | (g_wad[pos + 1] << 8);
  int patch_height = g_wad[pos + 2] | (g_wad[pos + 3] << 8);
  if (patch_width <= 0 || patch_width > 512 ||
      patch_height <= 0 || patch_height > 512 ||
      size < 8 + patch_width * 4)
    return;

  for (int sx = 0; sx < patch_width; sx++) {
    int x = originx + sx;
    if (x < 0 || x >= texture->width)
      continue;

    int col = pos + wad_le32(pos + 8 + sx * 4);
    if (col < pos || col >= pos + size)
      continue;

    while (col < pos + size) {
      int top = g_wad[col++];
      if (top == 255)
        break;
      if (col + 2 >= pos + size)
        break;
      int len = g_wad[col++];
      col++;
      if (col + len + 1 > pos + size)
        break;

      for (int py = 0; py < len; py++) {
        int y = originy + top + py;
        if (0 <= y && y < texture->height)
          texture->pixels[y * texture->width + x] = g_wad[col + py];
      }

      col += len + 1;
    }
  }
}

bool host_get_texture(const string& name, const HostTexture** out) {
  string key = normalize_lump_name(name);
  if (key.empty() || key == "-")
    return false;

  map<string, HostTexture>::iterator cached = g_host_texture_cache.find(key);
  if (cached != g_host_texture_cache.end()) {
    if (cached->second.width <= 0 || cached->second.height <= 0)
      return false;
    *out = &cached->second;
    return true;
  }

  HostTexture texture;
  texture.width = 0;
  texture.height = 0;

  int rec = 0;
  if (!host_find_texture_record(key, &rec)) {
    g_host_texture_cache[key] = texture;
    return false;
  }

  if (g_host_patch_lookup.empty())
    g_host_patch_lookup = load_patch_lookup_from_wad();
  if (g_host_patch_lookup.empty()) {
    g_host_texture_cache[key] = texture;
    return false;
  }

  int width = wad_le16s(rec + 12);
  int height = wad_le16s(rec + 14);
  int patch_count = wad_le16s(rec + 20);
  if (width <= 0 || width > 512 || height <= 0 || height > 512 ||
      patch_count <= 0 || patch_count > 128) {
    g_host_texture_cache[key] = texture;
    return false;
  }

  texture.width = width;
  texture.height = height;
  texture.pixels.assign(width * height, (byte)host_texture_color_base(key));

  int patches = rec + 22;
  for (int i = 0; i < patch_count; i++) {
    int patch = patches + i * 10;
    int patch_index = wad_le16s(patch + 4);
    if (patch_index < 0 || patch_index >= (int)g_host_patch_lookup.size())
      continue;
    host_blit_patch_to_texture(&texture, g_host_patch_lookup[patch_index],
                               wad_le16s(patch), wad_le16s(patch + 2));
  }

  g_host_texture_cache[key] = texture;
  *out = &g_host_texture_cache[key];
  return true;
}

bool host_get_flat(const string& name, const HostTexture** out) {
  string key = normalize_lump_name(name);
  if (key.empty() || key == "-")
    return false;

  map<string, HostTexture>::iterator cached = g_host_flat_cache.find(key);
  if (cached != g_host_flat_cache.end()) {
    if (cached->second.width <= 0 || cached->second.height <= 0)
      return false;
    *out = &cached->second;
    return true;
  }

  HostTexture flat;
  flat.width = 0;
  flat.height = 0;

  int lump = find_lump_index(key);
  if (lump < 0 || lump >= (int)g_wad_positions.size() ||
      g_wad_sizes[lump] < 4096) {
    g_host_flat_cache[key] = flat;
    return false;
  }

  int pos = g_wad_positions[lump];
  if (pos < 0 || pos + 4096 > (int)g_wad.size()) {
    g_host_flat_cache[key] = flat;
    return false;
  }

  flat.width = 64;
  flat.height = 64;
  flat.pixels.assign(g_wad.begin() + pos, g_wad.begin() + pos + 4096);
  g_host_flat_cache[key] = flat;
  *out = &g_host_flat_cache[key];
  return true;
}

void process_bfio_load_textures(const vector<byte>& args) {
  int count = read_u24_arg(args, 0);
  int textures = read_u24_arg(args, 1);
  int columnlump = read_u24_arg(args, 2);
  int columnofs = read_u24_arg(args, 3);
  int texturecomposite = read_u24_arg(args, 4);
  int compositesize = read_u24_arg(args, 5);
  int texturewidthmask = read_u24_arg(args, 6);
  int textureheight = read_u24_arg(args, 7);
  int texturetranslation = read_u24_arg(args, 8);
  int texturepool = read_u24_arg(args, 9);
  int texture_stride = read_u24_arg(args, 10);
  int column_lump_pool = read_u24_arg(args, 11);
  int column_ofs_pool = read_u24_arg(args, 12);
  int column_stride = read_u24_arg(args, 13);
  int width_off = read_u24_arg(args, 14);
  int height_off = read_u24_arg(args, 15);
  int patchcount_off = read_u24_arg(args, 16);
  int name_off = read_u24_arg(args, 17);
  int index_off = read_u24_arg(args, 18);
  int next_off = read_u24_arg(args, 19);
  int patches_off = read_u24_arg(args, 20);
  int originx_off = read_u24_arg(args, 21);
  int originy_off = read_u24_arg(args, 22);
  int patch_off = read_u24_arg(args, 23);
  int patch_stride = read_u24_arg(args, 24);

  vector<int> patch_lookup = load_patch_lookup_from_wad();
  int tex1 = find_lump_index("TEXTURE1");
  int tex2 = find_lump_index("TEXTURE2");
  int tex1_pos = tex1 >= 0 ? g_wad_positions[tex1] : 0;
  int tex2_pos = tex2 >= 0 ? g_wad_positions[tex2] : 0;
  int numtextures1 = tex1 >= 0 ? wad_le32(tex1_pos) : 0;
  int numtextures2 = tex2 >= 0 ? wad_le32(tex2_pos) : 0;
  int expected = numtextures1 + numtextures2;
  if (expected > 0 && count > expected)
    count = expected;

  g_texture_names.clear();

  for (int i = 0; i < count; i++) {
    bool second = i >= numtextures1;
    int lump_pos = second ? tex2_pos : tex1_pos;
    int lump_index = second ? i - numtextures1 : i;
    int offset = wad_le32(lump_pos + 4 + lump_index * 4);
    int rec = lump_pos + offset;
    int texture = texturepool + i * texture_stride;
    int collump = column_lump_pool + i * column_stride;
    int colofs = column_ofs_pool + i * column_stride;
    int width = wad_le16s(rec + 12);
    int height = wad_le16s(rec + 14);
    int patchcount = wad_le16s(rec + 20);
    int mask = 1;

    write_elvm_word(textures + i, texture);
    write_elvm_word(columnlump + i, collump);
    write_elvm_word(columnofs + i, colofs);
    write_elvm_word(texturecomposite + i, 0);
    write_elvm_word(compositesize + i, 0);
    write_elvm_word(texture + width_off, width);
    write_elvm_word(texture + height_off, height);
    write_elvm_word(texture + patchcount_off, patchcount);
    write_elvm_word(texture + index_off, i);
    write_elvm_word(texture + next_off, 0);

    string texname;
    for (int j = 0; j < 8; j++) {
      int c = g_wad[rec + j];
      write_elvm_byte(texture + name_off + j, c);
      if (c)
        texname.push_back(static_cast<char>(toupper(c)));
    }
    g_texture_names.push_back(texname);

    if (width <= 0 || width > column_stride || patchcount < 0 ||
        patchcount > 64) {
      fprintf(stderr, "[texhost bad texture %d width=%d patches=%d]",
              i, width, patchcount);
      continue;
    }

    for (int j = 0; j < patchcount; j++) {
      int patch_rec = rec + 22 + j * 10;
      int patch = texture + patches_off + j * patch_stride;
      int patchnum = wad_le16s(patch_rec + 4);
      int lump = 0 <= patchnum && patchnum < (int)patch_lookup.size()
                     ? patch_lookup[patchnum]
                     : 0;
      write_elvm_word(patch + originx_off, wad_le16s(patch_rec));
      write_elvm_word(patch + originy_off, wad_le16s(patch_rec + 2));
      write_elvm_word(patch + patch_off, lump);
    }

    while (mask * 2 <= width)
      mask <<= 1;
    write_elvm_word(texturewidthmask + i, mask - 1);
    write_elvm_word(textureheight + i, fixed_from_i16(height));
    write_elvm_word(texturetranslation + i, i);
  }
  write_elvm_word(texturetranslation + count, count);

  vector<byte> lookup_args;
  for (int i = 0; i < 13 * 3; i++)
    lookup_args.push_back(0);
  auto put_arg = [&](int index, int value) {
    int off = index * 3;
    lookup_args[off] = value & 255;
    lookup_args[off + 1] = (value >> 8) & 255;
    lookup_args[off + 2] = (value >> 16) & 255;
  };
  put_arg(0, count);
  put_arg(1, textures);
  put_arg(2, columnlump);
  put_arg(3, columnofs);
  put_arg(4, texturecomposite);
  put_arg(5, compositesize);
  put_arg(6, width_off);
  put_arg(7, height_off);
  put_arg(8, patchcount_off);
  put_arg(9, patches_off);
  put_arg(10, originx_off);
  put_arg(11, patch_off);
  put_arg(12, patch_stride);
  process_bfio_texture_lookup(lookup_args);
}

void process_bfio_fill_words(const vector<byte>& args) {
  int dest = read_u24_arg(args, 0);
  int count = read_u24_arg(args, 1);
  int value = read_u24_arg(args, 2);

  for (int i = 0; i < count; i++)
    write_elvm_word(dest + i, value);
}

void process_bfio_light_tables(const vector<byte>& args) {
  int zlight = read_u24_arg(args, 0);
  int colormaps = read_u24_arg(args, 1);
  int lightlevels = read_u24_arg(args, 2);
  int maxlightz = read_u24_arg(args, 3);
  int numcolormaps = read_u24_arg(args, 4);

  for (int i = 0; i < lightlevels; i++) {
    int startmap = ((lightlevels - 1 - i) * 2) * numcolormaps / lightlevels;
    for (int j = 0; j < maxlightz; j++) {
      int scale = 160 / (j + 1);
      int level = startmap - scale / 2;
      if (level < 0)
        level = 0;
      if (level >= numcolormaps)
        level = numcolormaps - 1;
      write_elvm_word(zlight + i * maxlightz + j, colormaps + level * 256);
    }
  }
}

int fixed_mul24(int a, int b, int fracbits) {
  return static_cast<int>((static_cast<long long>(a) * b) >> fracbits);
}

int fixed_div24(int a, int b, int fracbits) {
  if (b == 0)
    return a < 0 ? 0x800000 : 0x7fffff;
  if ((abs(a) >> 14) >= abs(b))
    return ((a ^ b) < 0) ? 0x800000 : 0x7fffff;
  return static_cast<int>((static_cast<long long>(a) << fracbits) / b);
}

void process_bfio_view_tables(const vector<byte>& args) {
  int viewangletox = read_u24_arg(args, 0);
  int xtoviewangle = read_u24_arg(args, 1);
  int yslope = read_u24_arg(args, 2);
  int distscale = read_u24_arg(args, 3);
  int scalelight = read_u24_arg(args, 4);
  int colormaps = read_u24_arg(args, 5);
  int finetangent = read_u24_arg(args, 6);
  int finecosine = read_u24_arg(args, 7);
  int centerxfrac = sign24(read_u24_arg(args, 8));
  int centerx = sign24(read_u24_arg(args, 9));
  (void)centerx;
  int viewwidth = sign24(read_u24_arg(args, 10));
  int viewheight = sign24(read_u24_arg(args, 11));
  int detailshift = sign24(read_u24_arg(args, 12));
  int fineangles = sign24(read_u24_arg(args, 13));
  int fieldofview = sign24(read_u24_arg(args, 14));
  int fracunit = sign24(read_u24_arg(args, 15));
  int fracbits = sign24(read_u24_arg(args, 16));
  int ang90 = sign24(read_u24_arg(args, 17));
  int angletofine = sign24(read_u24_arg(args, 18));
  int screenwidth = sign24(read_u24_arg(args, 19));
  int lightlevels = sign24(read_u24_arg(args, 20));
  int maxlightscale = sign24(read_u24_arg(args, 21));
  int numcolormaps = sign24(read_u24_arg(args, 22));
  int distmap = sign24(read_u24_arg(args, 23));

  int half_fine = fineangles / 2;
  int focal_index = fineangles / 4 + fieldofview / 2;
  int focal = fixed_div24(centerxfrac, read_elvm_word(finetangent + focal_index),
                          fracbits);

  for (int i = 0; i < half_fine; i++) {
    int tangent = read_elvm_word(finetangent + i);
    int t;
    if (tangent > fracunit * 2) {
      t = -1;
    } else if (tangent < -fracunit * 2) {
      t = viewwidth + 1;
    } else {
      t = fixed_mul24(tangent, focal, fracbits);
      t = (centerxfrac - t + fracunit - 1) >> fracbits;
      if (t < -1)
        t = -1;
      else if (t > viewwidth + 1)
        t = viewwidth + 1;
    }
    write_elvm_word(viewangletox + i, t);
  }

  for (int x = 0; x <= viewwidth; x++) {
    int i = 0;
    while (i < half_fine && read_elvm_word(viewangletox + i) > x)
      i++;
    write_elvm_word(xtoviewangle + x, (i << angletofine) - ang90);
  }

  for (int i = 0; i < half_fine; i++) {
    int current = read_elvm_word(viewangletox + i);
    if (current == -1)
      write_elvm_word(viewangletox + i, 0);
    else if (current == viewwidth + 1)
      write_elvm_word(viewangletox + i, viewwidth);
  }

  for (int i = 0; i < viewheight; i++) {
    int dy = ((i - viewheight / 2) << fracbits) + fracunit / 2;
    dy = abs(dy);
    write_elvm_word(yslope + i,
                    fixed_div24((viewwidth << detailshift) / 2 * fracunit,
                                dy, fracbits));
  }

  for (int i = 0; i < viewwidth; i++) {
    int angle = read_elvm_word_raw(xtoviewangle + i);
    int fine = (angle >> angletofine) & (fineangles - 1);
    int cosadj = abs(read_elvm_word(finecosine + fine));
    write_elvm_word(distscale + i, fixed_div24(fracunit, cosadj, fracbits));
  }

  for (int i = 0; i < lightlevels; i++) {
    int startmap = ((lightlevels - 1 - i) * 2) * numcolormaps / lightlevels;
    for (int j = 0; j < maxlightscale; j++) {
      int level = startmap - j * screenwidth /
                               (viewwidth << detailshift) / distmap;
      if (level < 0)
        level = 0;
      if (level >= numcolormaps)
        level = numcolormaps - 1;
      write_elvm_word(scalelight + i * maxlightscale + j,
                      colormaps + level * 256);
    }
  }
}

void process_bfio_translation_tables(const vector<byte>& args) {
  int dest = read_u24_arg(args, 0);

  for (int i = 0; i < 256; i++) {
    int gray;
    int brown;
    int red;
    if (i >= 0x70 && i <= 0x7f) {
      gray = 0x60 + (i & 0xf);
      brown = 0x40 + (i & 0xf);
      red = 0x20 + (i & 0xf);
    } else {
      gray = brown = red = i;
    }
    write_elvm_byte(dest + i, gray);
    write_elvm_byte(dest + i + 256, brown);
    write_elvm_byte(dest + i + 512, red);
  }
}

void draw_host_line(int screen, int width, int height, int x0, int y0,
                    int x1, int y1, int color) {
  int dx = abs(x1 - x0);
  int sx = x0 < x1 ? 1 : -1;
  int dy = -abs(y1 - y0);
  int sy = y0 < y1 ? 1 : -1;
  int err = dx + dy;

  while (true) {
    if (0 <= x0 && x0 < width && 0 <= y0 && y0 < height)
      write_elvm_byte(screen + y0 * width + x0, color);
    if (x0 == x1 && y0 == y1)
      break;
    int e2 = 2 * err;
    if (e2 >= dy) {
      err += dy;
      x0 += sx;
    }
    if (e2 <= dx) {
      err += dx;
      y0 += sy;
    }
  }
}

void draw_host_rect(int screen, int width, int height, int x0, int y0,
                    int x1, int y1, int color) {
  if (x0 < 0)
    x0 = 0;
  if (y0 < 0)
    y0 = 0;
  if (x1 > width)
    x1 = width;
  if (y1 > height)
    y1 = height;
  for (int y = y0; y < y1; y++)
    for (int x = x0; x < x1; x++)
      write_elvm_byte(screen + y * width + x, color);
}

bool draw_host_wad_patch(int screen, int width, int height, const char* name,
                         int center_x, int bottom_y, int draw_h);

const char* host_status_face_patch() {
  if (g_host_health <= 0)
    return "STFDEAD0";

  int pain = (100 - g_host_health) * 5 / 101;
  if (pain < 0)
    pain = 0;
  if (pain > 4)
    pain = 4;

  static char patch[9];
  if (g_host_damage_flash > 0)
    snprintf(patch, sizeof(patch), "STFOUCH%d", pain);
  else
    snprintf(patch, sizeof(patch), "STFST%d0", pain);
  return patch;
}

bool draw_host_patch_number(int screen, int width, int height, int right_x,
                            int top_y, int value, int min_digits, int draw_h,
                            int advance) {
  if (value < 0)
    value = 0;
  if (value > 999)
    value = 999;

  int digits[3] = {value / 100, (value / 10) % 10, value % 10};
  int start = min_digits >= 3 || value >= 100 ? 0
              : (value >= 10 || min_digits >= 2 ? 1 : 2);
  int count = 3 - start;
  bool drew = false;

  for (int i = start; i < 3; i++) {
    char patch[9];
    snprintf(patch, sizeof(patch), "STTNUM%d", digits[i]);
    int slot = i - start;
    int center_x = right_x - (count - slot - 1) * advance - advance / 2;
    if (draw_host_wad_patch(screen, width, height, patch, center_x,
                            top_y + draw_h, draw_h)) {
      drew = true;
    }
  }

  return drew;
}

void draw_host_status_bar(int screen, int width, int height) {
  int bar_h = height >= 320 ? 64 : 32;
  int y = height - bar_h;
  int status_scale = bar_h / 32;
  if (status_scale < 1)
    status_scale = 1;
  draw_host_rect(screen, width, height, 0, y, width, height, 96);
  if (!draw_host_wad_patch(screen, width, height, "STBAR", width / 2, height,
                           bar_h)) {
    draw_host_rect(screen, width, height, 0, y, width, height, 7);
    draw_host_rect(screen, width, height, 0, y, width, y + 2, 80);
    draw_host_rect(screen, width, height, 4, y + 4, width / 3 - 4, height - 3,
                   96);
    draw_host_rect(screen, width, height, width / 3 + 2, y + 4,
                   width * 2 / 3 - 2, height - 3, 96);
    draw_host_rect(screen, width, height, width * 2 / 3 + 4, y + 4,
                   width - 4, height - 3, 96);
  }
  int number_top = y + 3 * status_scale;
  int number_h = 16 * status_scale;
  int number_advance = 14 * status_scale;
  bool wad_numbers = true;
  wad_numbers = draw_host_patch_number(screen, width, height,
                                       44 * status_scale, number_top,
                                       host_current_ammo(), 1, number_h,
                                       number_advance) && wad_numbers;
  wad_numbers = draw_host_patch_number(screen, width, height,
                                       90 * status_scale, number_top,
                                       g_host_health, 1, number_h,
                                       number_advance) && wad_numbers;
  wad_numbers = draw_host_wad_patch(screen, width, height, "STTPRCNT",
                                    97 * status_scale, number_top + number_h,
                                    number_h) && wad_numbers;
  wad_numbers = draw_host_wad_patch(screen, width, height,
                                    host_status_face_patch(),
                                    155 * status_scale,
                                    y + 29 * status_scale,
                                    29 * status_scale) && wad_numbers;
  wad_numbers = draw_host_patch_number(screen, width, height,
                                       221 * status_scale, number_top,
                                       g_host_armor, 1, number_h,
                                       number_advance) && wad_numbers;
  wad_numbers = draw_host_wad_patch(screen, width, height, "STTPRCNT",
                                    228 * status_scale, number_top + number_h,
                                    number_h) && wad_numbers;
  (void)wad_numbers;
}

void draw_host_flat_background(int screen, int width, int height, double px,
                               double py, double base_angle, double fov) {
  const HostTexture* floor_flat = NULL;
  const HostTexture* ceiling_flat = NULL;
  const HostTexture* sky = NULL;
  bool have_floor = host_get_flat(g_host_floor_flat, &floor_flat);
  bool have_ceiling = host_get_flat(g_host_ceiling_flat, &ceiling_flat);
  bool have_sky = g_host_has_sky && host_get_texture("SKY1", &sky);
  int mid = height / 2;

  for (int y = 0; y < height; y++) {
    bool ceiling = y < mid;
    const HostTexture* flat = ceiling ? ceiling_flat : floor_flat;
    bool have_flat = ceiling ? have_ceiling : have_floor;
    int row = ceiling ? mid - y : y - mid + 1;
    if (row < 1)
      row = 1;

    for (int x = 0; x < width; x++) {
      int color;
      if (ceiling && have_sky) {
        double rel = (double)x / (double)(width - 1) - 0.5;
        double sky_angle = normalize_host_angle(base_angle + rel * fov);
        double u = (sky_angle + kPi) / (2.0 * kPi);
        int sx = static_cast<int>(floor(u * sky->width)) % sky->width;
        if (sx < 0)
          sx += sky->width;
        int sy = (y * sky->height) / mid;
        if (sy < 0)
          sy = 0;
        if (sy >= sky->height)
          sy = sky->height - 1;
        color = sky->pixels[sy * sky->width + sx];
      } else if (have_flat) {
        double ray_angle =
            base_angle + ((double)x / (double)(width - 1) - 0.5) * fov;
        double dist = (height * 22.0) / row;
        int fx = static_cast<int>(floor(px + cos(ray_angle) * dist)) & 63;
        int fy = static_cast<int>(floor(py + sin(ray_angle) * dist)) & 63;
        color = flat->pixels[fy * 64 + fx];
      } else if (ceiling) {
        int band = (mid - y) / 16;
        color = 104 + (band & 3);
      } else {
        int band = (y - mid) / 12;
        color = 56 + (band & 7);
      }
      write_elvm_byte(screen + y * width + x, color);
    }
  }
}

const char* host_actor_patch_name(int type) {
  switch (type) {
    case 9:
      return "SPOSA1";
    case 3004:
      return "POSSA1";
    case 3001:
      return "TROOA1";
    case 3002:
    case 58:
      return "SARGA1";
    case 3003:
      return "BOSSA1";
    case 3005:
      return "HEADA1";
    case 2001:
      return "SHOTA0";
    case 2002:
      return "MGUNA0";
    case 2003:
      return "LAUNA0";
    case 2005:
      return "CSAWA0";
    case 2007:
      return "CLIPA0";
    case 2008:
      return "SHELA0";
    case 2010:
      return "ROCKA0";
    case 2011:
      return "STIMA0";
    case 2012:
      return "MEDIA0";
    case 2013:
      return "SOULA0";
    case 2014:
      return "BON1A0";
    case 2015:
      return "BON2A0";
    case 2018:
      return "ARM1A0";
    case 2019:
      return "ARM2A0";
    case 2046:
      return "BROKA0";
    case 2048:
      return "AMMOA0";
    case 2049:
      return "SBOXA0";
    default:
      return NULL;
  }
}

const char* host_actor_sprite_prefix(int type) {
  switch (type) {
    case 9:
      return "SPOS";
    case 3004:
      return "POSS";
    case 3001:
      return "TROO";
    case 3002:
    case 58:
      return "SARG";
    case 3003:
      return "BOSS";
    case 3005:
      return "HEAD";
    case 3006:
      return "SKUL";
    default:
      return NULL;
  }
}

int host_actor_rotation_for_view(const HostActor& actor) {
  double to_viewer = atan2(g_host_player_y - actor.y, g_host_player_x - actor.x);
  double rel = normalize_host_angle(to_viewer - host_actor_angle_rad(actor));
  int sector = static_cast<int>(floor((rel + kPi / 8.0) / (kPi / 4.0)));
  sector %= 8;
  if (sector < 0)
    sector += 8;
  static const int rotations[8] = {1, 2, 3, 4, 5, 6, 7, 8};
  return rotations[sector];
}

const char* host_actor_death_frames(int type) {
  switch (type) {
    case 9:
    case 3004:
      return "HIJKL";
    case 3001:
      return "IJKLM";
    case 58:
    case 3002:
      return "IJKLMN";
    case 3003:
      return "IJKLMNO";
    case 3005:
      return "GHIJKL";
    case 3006:
      return "FGHIJK";
    default:
      return "IJKLM";
  }
}

char host_actor_pain_frame(int type) {
  switch (type) {
    case 9:
    case 3004:
      return 'G';
    case 3005:
      return 'E';
    case 3006:
      return 'D';
    case 3003:
      return 'I';
    default:
      return 'H';
  }
}

const char* host_actor_attack_frames(int type) {
  switch (type) {
    case 9:
    case 3004:
      return "EFE";
    default:
      return "EFG";
  }
}

char host_actor_frame_for_state(const HostActor& actor) {
  if (!actor.alive) {
    const char* frames = host_actor_death_frames(actor.type);
    int count = strlen(frames);
    int index = actor.death_tics / 5;
    if (index < 0)
      index = 0;
    if (index >= count)
      index = count - 1;
    return frames[index];
  }
  if (actor.flash > 0)
    return host_actor_pain_frame(actor.type);
  if (actor.attack_tics > 0) {
    const char* frames = host_actor_attack_frames(actor.type);
    int count = strlen(frames);
    int index = (24 - actor.attack_tics) / 8;
    if (index < 0)
      index = 0;
    if (index >= count)
      index = count - 1;
    return frames[index];
  }
  if (!actor.awake)
    return ((g_host_tick / 10) & 1) ? 'B' : 'A';

  static const char walk_frames[4] = {'A', 'B', 'C', 'D'};
  return walk_frames[(g_host_tick / 3) & 3];
}

const char* host_actor_visual_state(const HostActor& actor) {
  if (!actor.alive)
    return "death";
  if (actor.flash > 0)
    return "pain";
  if (actor.attack_tics > 0)
    return "attack";
  return actor.awake ? "chase" : "stand";
}

string host_actor_frame_lump_name(const char* prefix, char frame, int rot,
                                  bool* flipped) {
  if (flipped)
    *flipped = false;
  if (rot < 1 || rot > 8)
    rot = 1;

  string base(prefix);
  base += frame;
  if (rot > 5) {
    int mirror_rot = 10 - rot;
    string packed = base + static_cast<char>('0' + mirror_rot) + frame +
                    static_cast<char>('0' + rot);
    if (find_lump_index(packed) >= 0) {
      if (flipped)
        *flipped = true;
      return packed;
    }
  } else if (rot >= 2 && rot <= 4) {
    int mirror_rot = 10 - rot;
    string packed = base + static_cast<char>('0' + rot) + frame +
                    static_cast<char>('0' + mirror_rot);
    if (find_lump_index(packed) >= 0)
      return packed;
  }

  string direct = base + static_cast<char>('0' + rot);
  if (find_lump_index(direct) >= 0)
    return direct;

  string unrotated = base + "0";
  if (find_lump_index(unrotated) >= 0)
    return unrotated;

  return "";
}

string host_actor_patch_name_for_view(const HostActor& actor, bool* flipped,
                                      int* rotation, char* frame_out) {
  if (flipped)
    *flipped = false;
  if (rotation)
    *rotation = 0;
  if (frame_out)
    *frame_out = 'A';

  const char* prefix = host_actor_sprite_prefix(actor.type);
  if (!prefix)
    return host_actor_patch_name(actor.type) ? host_actor_patch_name(actor.type) : "";

  int rot = host_actor_rotation_for_view(actor);
  char frame = host_actor_frame_for_state(actor);
  if (rotation)
    *rotation = rot;
  if (frame_out)
    *frame_out = frame;

  string name = host_actor_frame_lump_name(prefix, frame, rot, flipped);
  if (!name.empty())
    return name;
  return string(prefix) + "A1";
}

bool draw_host_wad_patch_sprite(int screen, int width, int height, int sx,
                                int size, double dist,
                                const vector<double>& zbuffer,
                                const HostActor& actor) {
  bool flipped = false;
  int rotation = 0;
  char frame = 'A';
  string name = host_actor_patch_name_for_view(actor, &flipped, &rotation,
                                              &frame);
  if (name.empty())
    return false;

  int lump = find_lump_index(name);
  if (lump < 0 || lump >= (int)g_wad_positions.size())
    return false;

  int pos = g_wad_positions[lump];
  int lump_size = g_wad_sizes[lump];
  if (lump_size < 8 || pos < 0 || pos + lump_size > (int)g_wad.size())
    return false;

  int patch_width = g_wad[pos] | (g_wad[pos + 1] << 8);
  int patch_height = g_wad[pos + 2] | (g_wad[pos + 3] << 8);
  if (patch_width <= 0 || patch_width > 512 ||
      patch_height <= 0 || patch_height > 512 ||
      lump_size < 8 + patch_width * 4)
    return false;

  int draw_h = size;
  int draw_w = patch_width * draw_h / patch_height;
  if (draw_w < 2)
    draw_w = 2;
  if (draw_w > width)
    draw_w = width;
  int x_left = sx - draw_w / 2;
  int y_top = height / 2 - draw_h / 2;

  for (int dx = 0; dx < draw_w; dx++) {
    int x = x_left + dx;
    if (x < 0 || x >= width || dist > zbuffer[x])
      continue;

    int src_x = dx * patch_width / draw_w;
    if (flipped)
      src_x = patch_width - 1 - src_x;
    int col_ofs = wad_le32(pos + 8 + src_x * 4);
    int col = pos + col_ofs;
    if (col < pos || col >= pos + lump_size)
      continue;

    while (col < pos + lump_size) {
      int top = g_wad[col++];
      if (top == 255)
        break;
      if (col + 2 >= pos + lump_size)
        break;
      int len = g_wad[col++];
      col++;
      if (col + len + 1 > pos + lump_size)
        break;

      for (int py = 0; py < len; py++) {
        int src_y = top + py;
        int y0 = y_top + src_y * draw_h / patch_height;
        int y1 = y_top + (src_y + 1) * draw_h / patch_height;
        if (y1 <= y0)
          y1 = y0 + 1;
        byte color = actor.flash > 0 ? (byte)(160 + (actor.flash & 3))
                                     : g_wad[col + py];
        for (int y = y0; y < y1; y++) {
          if (0 <= y && y < height)
            write_elvm_byte(screen + y * width + x, color);
        }
      }

      col += len + 1;
    }
  }

  if (!g_host_logged_rotated_sprite && rotation > 1 && !isatty(STDERR_FILENO)) {
    fprintf(stderr, "sprite type=%d frame=%c rotation=%d lump=%s flip=%d\n",
            actor.type, frame, rotation, name.c_str(), flipped ? 1 : 0);
    g_host_logged_rotated_sprite = true;
  }
  if (!g_host_logged_animated_sprite && frame != 'A' &&
      !isatty(STDERR_FILENO)) {
    fprintf(stderr, "sprite_anim type=%d frame=%c rotation=%d lump=%s flip=%d\n",
            actor.type, frame, rotation, name.c_str(), flipped ? 1 : 0);
    g_host_logged_animated_sprite = true;
  }
  const char* visual_state = host_actor_visual_state(actor);
  bool* logged_state = NULL;
  if (strcmp(visual_state, "attack") == 0)
    logged_state = &g_host_logged_attack_sprite;
  else if (strcmp(visual_state, "pain") == 0)
    logged_state = &g_host_logged_pain_sprite;
  else if (strcmp(visual_state, "death") == 0)
    logged_state = &g_host_logged_death_sprite;
  if (logged_state && !*logged_state && !isatty(STDERR_FILENO)) {
    fprintf(stderr,
            "sprite_state type=%d state=%s frame=%c rotation=%d lump=%s flip=%d\n",
            actor.type, visual_state, frame, rotation, name.c_str(),
            flipped ? 1 : 0);
    *logged_state = true;
  }
  return true;
}

bool draw_host_wad_patch(int screen, int width, int height, const char* name,
                         int center_x, int bottom_y, int draw_h) {
  int lump = find_lump_index(name);
  if (lump < 0 || lump >= (int)g_wad_positions.size())
    return false;

  int pos = g_wad_positions[lump];
  int lump_size = g_wad_sizes[lump];
  if (lump_size < 8 || pos < 0 || pos + lump_size > (int)g_wad.size())
    return false;

  int patch_width = g_wad[pos] | (g_wad[pos + 1] << 8);
  int patch_height = g_wad[pos + 2] | (g_wad[pos + 3] << 8);
  if (patch_width <= 0 || patch_width > 512 ||
      patch_height <= 0 || patch_height > 512 ||
      lump_size < 8 + patch_width * 4)
    return false;

  int draw_w = patch_width * draw_h / patch_height;
  if (draw_w < 2)
    draw_w = 2;
  int x_left = center_x - draw_w / 2;
  int y_top = bottom_y - draw_h;

  for (int dx = 0; dx < draw_w; dx++) {
    int x = x_left + dx;
    if (x < 0 || x >= width)
      continue;

    int src_x = dx * patch_width / draw_w;
    int col = pos + wad_le32(pos + 8 + src_x * 4);
    if (col < pos || col >= pos + lump_size)
      continue;

    while (col < pos + lump_size) {
      int top = g_wad[col++];
      if (top == 255)
        break;
      if (col + 2 >= pos + lump_size)
        break;
      int len = g_wad[col++];
      col++;
      if (col + len + 1 > pos + lump_size)
        break;

      for (int py = 0; py < len; py++) {
        int src_y = top + py;
        int y0 = y_top + src_y * draw_h / patch_height;
        int y1 = y_top + (src_y + 1) * draw_h / patch_height;
        if (y1 <= y0)
          y1 = y0 + 1;
        for (int y = y0; y < y1; y++) {
          if (0 <= y && y < height)
            write_elvm_byte(screen + y * width + x, g_wad[col + py]);
        }
      }

      col += len + 1;
    }
  }

  return true;
}

void host_collect_pickups();

void process_bfio_render_map_view(const vector<byte>& args) {
  g_last_render_args = args;
  int screen = read_u24_arg(args, 0);
  int mobj = read_u24_arg(args, 1);
  int lines = read_u24_arg(args, 2);
  int numlines = read_u24_arg(args, 3);
  int line_stride = read_u24_arg(args, 4);
  int line_v1_off = read_u24_arg(args, 5);
  int line_v2_off = read_u24_arg(args, 6);
  (void)read_u24_arg(args, 7);
  int vertex_x_off = read_u24_arg(args, 8);
  int vertex_y_off = read_u24_arg(args, 9);
  int mobj_x_off = read_u24_arg(args, 10);
  int mobj_y_off = read_u24_arg(args, 11);
  int mobj_angle_off = read_u24_arg(args, 12);
  int width = read_u24_arg(args, 13);
  int height = read_u24_arg(args, 14);
  (void)lines;
  (void)numlines;
  (void)line_stride;
  (void)line_v1_off;
  (void)line_v2_off;
  (void)vertex_x_off;
  (void)vertex_y_off;

  if (width <= 0 || width > 640 || height <= 0 || height > 480)
    return;

  (void)mobj;
  (void)mobj_x_off;
  (void)mobj_y_off;
  (void)mobj_angle_off;
  ensure_host_map();
  ensure_host_player_start();
  double px = g_host_player_x;
  double py = g_host_player_y;
  double base_angle = g_host_player_angle;
  double fov = kPi / 2.4;
  ensure_host_actors();
  host_collect_pickups();
  host_update_enemies();
  g_host_rendered_actors.clear();
  g_host_render_width = width;
  vector<double> zbuffer(width, 1.0e30);

  draw_host_flat_background(screen, width, height, px, py, base_angle, fov);

  for (int sx = 0; sx < width; sx++) {
    double ray_angle = base_angle + ((double)sx / (double)(width - 1) - 0.5) * fov;
    double rdx = cos(ray_angle);
    double rdy = sin(ray_angle);
    double best = 1.0e30;
    double best_u = 0.0;
    int best_line = -1;

    for (int i = 0; i < (int)g_host_lines.size(); i++) {
      if (!g_host_lines[i].solid)
        continue;
      double x1 = g_host_lines[i].x1;
      double y1 = g_host_lines[i].y1;
      double x2 = g_host_lines[i].x2;
      double y2 = g_host_lines[i].y2;
      double sdx = x2 - x1;
      double sdy = y2 - y1;
      double denom = rdx * sdy - rdy * sdx;
      if (fabs(denom) < 0.000001)
        continue;

      double qx = x1 - px;
      double qy = y1 - py;
      double t = (qx * sdy - qy * sdx) / denom;
      double u = (qx * rdy - qy * rdx) / denom;
      if (t > 1.0 && u >= 0.0 && u <= 1.0 && t < best) {
        best = t;
        best_u = u;
        best_line = i;
      }
    }

    if (best < 1.0e20) {
      double corrected = best * cos(ray_angle - base_angle);
      int wall = static_cast<int>(height * 48.0 / (corrected + 1.0));
      if (wall < 2)
        wall = 2;
      if (wall > height - 6)
        wall = height - 6;
      int y0 = (height - wall) / 2;
      int y1 = y0 + wall;
      zbuffer[sx] = corrected;
      int distance_light = 7 - static_cast<int>(corrected / 128.0);
      if (distance_light < 0)
        distance_light = 0;
      if (distance_light > 7)
        distance_light = 7;
      int sector_light = best_line < 0 ? 0 : (g_host_lines[best_line].light - 128) / 64;
      if (sector_light < 0)
        sector_light = 0;
      if (sector_light > 2)
        sector_light = 2;
      int base = best_line < 0 ? 96 : g_host_lines[best_line].color_base;
      const HostTexture* texture = NULL;
      bool use_texture = best_line >= 0 &&
                         host_get_texture(g_host_lines[best_line].texture,
                                          &texture);
      int max_shade = use_texture ? 255 : base + 15;
      int tex_x = 0;
      if (use_texture) {
        tex_x = static_cast<int>(best_u * texture->width);
        if (tex_x < 0)
          tex_x = 0;
        if (tex_x >= texture->width)
          tex_x = texture->width - 1;
      }
      for (int y = y0; y < y1 && y < height; y++) {
        int mortar = ((sx & 31) == 0) || ((y & 15) == 0);
        int pattern = ((sx >> 3) ^ (y >> 4) ^ best_line) & 3;
        int shade = mortar ? base + 13 : base + distance_light + sector_light + (pattern == 0 ? 3 : 0);
        if (use_texture) {
          int tex_y = (y - y0) * texture->height / wall;
          if (tex_y < 0)
            tex_y = 0;
          if (tex_y >= texture->height)
            tex_y = texture->height - 1;
          shade = texture->pixels[tex_y * texture->width + tex_x];
        }
        if (shade > max_shade)
          shade = max_shade;
        write_elvm_byte(screen + y * width + sx, shade);
      }
    }
  }

  int cx = width / 2;
  int cy = height / 2;

  for (size_t i = 0; i < g_host_actors.size(); i++) {
    HostActor& actor = g_host_actors[i];
    if (!actor.alive &&
        (!host_actor_is_enemy(actor.type) || actor.death_tics <= 0))
      continue;

    double dx = actor.x - px;
    double dy = actor.y - py;
    double dist = sqrt(dx * dx + dy * dy);
    if (dist < 8.0)
      continue;

    double rel = normalize_host_angle(atan2(dy, dx) - base_angle);
    if (fabs(rel) > fov * 0.55)
      continue;

    int sx = static_cast<int>((rel / fov + 0.5) * (width - 1));
    int size = static_cast<int>(height * 5.0 / (dist + 1.0));
    if (host_actor_is_enemy(actor.type)) {
      size = static_cast<int>(size * 1.4);
      if (size < 10)
        size = 10;
      if (size > height / 2)
        size = height / 2;
    } else {
      size = static_cast<int>(size * 0.75);
      if (size < 7)
        size = 7;
      if (size > height / 4)
        size = height / 4;
    }

    if (host_actor_is_enemy(actor.type)) {
      int x0 = sx - size / 2;
      int x1 = sx + size / 2;
      int visible_x0 = width;
      int visible_x1 = -1;
      for (int x = std::max(0, x0); x <= std::min(width - 1, x1); x++) {
        if (dist <= zbuffer[x]) {
          if (x < visible_x0)
            visible_x0 = x;
          if (x > visible_x1)
            visible_x1 = x;
        }
      }
      if (visible_x1 >= visible_x0) {
        HostRenderedActor rendered;
        rendered.index = static_cast<int>(i);
        rendered.dist = dist;
        rendered.x0 = visible_x0;
        rendered.x1 = visible_x1;
        g_host_rendered_actors.push_back(rendered);
      }
    }

    draw_host_wad_patch_sprite(screen, width, height, sx, size, dist, zbuffer,
                               actor);
    if (actor.flash > 0)
      actor.flash--;
  }

  draw_host_line(screen, width, height, cx - 5, cy, cx + 5, cy, 4);
  draw_host_line(screen, width, height, cx, cy - 5, cx, cy + 5, 4);

  int status_h = height >= 320 ? 64 : 32;
  int weapon_h = height / 3;
  int weapon_bottom = height - status_h + 8;
  const char* idle_patch = host_weapon_patch_name(false);
  draw_host_wad_patch(screen, width, height, idle_patch, cx, weapon_bottom,
                      weapon_h);

  if (g_host_fire_flash > 0) {
    const char* fire_patch = host_weapon_patch_name(true);
    draw_host_wad_patch(screen, width, height, fire_patch, cx, weapon_bottom,
                        weapon_h);
    g_host_fire_flash--;
  }

  draw_host_status_bar(screen, width, height);

  if (g_host_pickup_flash > 0) {
    draw_host_rect(screen, width, height, 0, height - 3, width, height, 160);
    g_host_pickup_flash--;
  }
  if (g_host_level_complete) {
    draw_host_rect(screen, width, height, 0, 0, width, 4, 112);
    draw_host_rect(screen, width, height, 0, height - 5, width, height, 112);
  }
  if (g_host_damage_flash > 0) {
    int color = 32 + (g_host_damage_flash & 7);
    draw_host_rect(screen, width, height, 0, 0, width, 3, color);
    draw_host_rect(screen, width, height, 0, height - 3, width, height, color);
    draw_host_rect(screen, width, height, 0, 0, 3, height, color);
    draw_host_rect(screen, width, height, width - 3, 0, width, height, color);
    g_host_damage_flash--;
  }
}

HostSpriteFrame empty_sprite_frame() {
  HostSpriteFrame frame;
  frame.rotate = -1;
  for (int i = 0; i < 8; i++) {
    frame.lump[i] = -1;
    frame.flip[i] = -1;
  }
  return frame;
}

void host_install_sprite_lump(vector<HostSpriteFrame>& frames, int* maxframe,
                              int firstsprite, int lump, int frame,
                              int rotation, bool flipped) {
  if (frame < 0 || frame >= 29 || rotation < 0 || rotation > 8)
    return;

  if (frame > *maxframe)
    *maxframe = frame;

  HostSpriteFrame& temp = frames[frame];
  int relative_lump = lump - firstsprite;

  if (rotation == 0) {
    temp.rotate = 0;
    for (int r = 0; r < 8; r++) {
      temp.lump[r] = relative_lump;
      temp.flip[r] = flipped ? 1 : 0;
    }
    return;
  }

  rotation--;
  temp.rotate = 1;
  temp.lump[rotation] = relative_lump;
  temp.flip[rotation] = flipped ? 1 : 0;
}

void process_bfio_prepare_sprite_defs(const vector<byte>& args) {
  int namelist = read_u24_arg(args, 0);
  int numsprites = read_u24_arg(args, 1);
  int firstsprite = read_u24_arg(args, 2);
  int lastsprite = read_u24_arg(args, 3);
  int modified = read_u24_arg(args, 4);
  int maxframes = read_u24_arg(args, 5);

  g_prepared_sprites.clear();
  g_prepared_sprites.resize(numsprites);

  for (int i = 0; i < numsprites; i++) {
    int name_ptr = read_elvm_word_raw(namelist + i);
    string sprite_name = read_elvm_name(name_ptr, 4);
    vector<HostSpriteFrame> frames(29);
    int maxframe = -1;

    for (int f = 0; f < 29; f++)
      frames[f] = empty_sprite_frame();

    for (int lump = firstsprite; lump <= lastsprite; lump++) {
      if (lump < 0 || lump >= (int)g_wad_lumps.size())
        continue;

      const string& lump_name = g_wad_lumps[lump];
      if (lump_name.size() < 6 || lump_name.substr(0, 4) != sprite_name)
        continue;

      int patched = lump;
      if (modified)
        patched = find_lump_index(lump_name);
      if (patched < 0)
        patched = lump;

      host_install_sprite_lump(frames, &maxframe, firstsprite, patched,
                               lump_name[4] - 'A', lump_name[5] - '0', false);

      if (lump_name.size() >= 8) {
        host_install_sprite_lump(frames, &maxframe, firstsprite, lump,
                                 lump_name[6] - 'A', lump_name[7] - '0', true);
      }
    }

    int frame_count = maxframe < 0 ? 0 : maxframe + 1;
    g_prepared_sprites[i].assign(frames.begin(), frames.begin() + frame_count);
    write_elvm_word(maxframes + i, frame_count);
  }
}

void process_bfio_fill_sprite_defs(const vector<byte>& args) {
  int sprites_addr = read_u24_arg(args, 0);
  int numsprites = read_u24_arg(args, 1);
  int spritedef_stride = read_u24_arg(args, 2);
  int spriteframes_off = read_u24_arg(args, 3);
  int spriteframe_stride = read_u24_arg(args, 4);
  int rotate_off = read_u24_arg(args, 5);
  int lump_off = read_u24_arg(args, 6);
  int flip_off = read_u24_arg(args, 7);

  for (int i = 0; i < numsprites && i < (int)g_prepared_sprites.size(); i++) {
    int sprdef = sprites_addr + i * spritedef_stride;
    int frames_addr = read_elvm_word_raw(sprdef + spriteframes_off);
    const vector<HostSpriteFrame>& frames = g_prepared_sprites[i];

    for (int f = 0; f < (int)frames.size(); f++) {
      int frame_addr = frames_addr + f * spriteframe_stride;
      write_elvm_word(frame_addr + rotate_off, frames[f].rotate);
      for (int r = 0; r < 8; r++) {
        write_elvm_word(frame_addr + lump_off + r, frames[f].lump[r]);
        write_elvm_word(frame_addr + flip_off + r, frames[f].flip[r]);
      }
    }
  }
}

void process_bfio_draw_frame(const vector<byte>& args) {
  g_last_draw_args = args;
  int screen = read_u24_arg(args, 0);
  int colors = read_u24_arg(args, 1);
  int width = read_u24_arg(args, 2);
  int height = read_u24_arg(args, 3);
  int scale = read_u24_arg(args, 4);
  int color_stride = read_u24_arg(args, 5);
  int r_off = read_u24_arg(args, 6);
  int g_off = read_u24_arg(args, 7);
  int b_off = read_u24_arg(args, 8);

  if (width <= 0 || height <= 0 || scale <= 0)
    return;

  int out_width = width * scale;
  int out_height = height * scale;
  if (out_width != kFrameWidth || out_height != kFrameHeight)
    return;

  vector<byte> frame(kFrameBytes);

  for (int y = 0; y < kFrameHeight; y++) {
    int sy = y / scale;
    for (int x = 0; x < kFrameWidth; x++) {
      int sx = x / scale;
      int color_index = read_elvm_word_raw(screen + sy * width + sx) & 255;
      int color_addr = colors + color_index * color_stride;
      int out = (y * kFrameWidth + x) * 3;

      frame[out] = read_elvm_word_raw(color_addr + r_off) & 255;
      frame[out + 1] = read_elvm_word_raw(color_addr + g_off) & 255;
      frame[out + 2] = read_elvm_word_raw(color_addr + b_off) & 255;
      if (color_index != 0 && frame[out] == 0 && frame[out + 1] == 0 &&
          frame[out + 2] == 0) {
        int base = color_index & 15;
        if (color_index >= 100) {
          frame[out] = 80 + base * 4;
          frame[out + 1] = 80 + base * 4;
          frame[out + 2] = 90 + base * 5;
        } else if (color_index >= 64) {
          frame[out] = 36 + base * 3;
          frame[out + 1] = 92 + base * 5;
          frame[out + 2] = 48 + base * 2;
        } else {
          frame[out] = 55 + base * 4;
          frame[out + 1] = 42 + base * 3;
          frame[out + 2] = 30 + base * 2;
        }
      }
    }
  }

  handle_frame(frame);
}

string numbered_lump_name(const string& prefix, int value, int digits) {
  char buf[32];
  snprintf(buf, sizeof(buf), "%s%0*d", prefix.c_str(), digits, value);
  return string(buf);
}

int copy_wad_lump_to_elvm(const string& name, int dest) {
  int lump = find_lump_index(name);
  if (lump < 0 || lump >= (int)g_wad_positions.size())
    return 0;

  int pos = g_wad_positions[lump];
  int size = g_wad_sizes[lump];
  for (int i = 0; i < size; i++) {
    int p = pos + i;
    write_elvm_byte(dest + i, 0 <= p && p < (int)g_wad.size() ? g_wad[p] : 0);
  }
  return size;
}

void process_bfio_hu_fonts(const vector<byte>& args) {
  int font = read_u24_arg(args, 0);
  int storage = read_u24_arg(args, 1);
  int first = read_u24_arg(args, 2);
  int count = read_u24_arg(args, 3);
  int cursor = storage;

  for (int i = 0; i < count; i++) {
    string name = numbered_lump_name("STCFN", first + i, 3);
    int size = copy_wad_lump_to_elvm(name, cursor);
    write_elvm_word(font + i, cursor);
    cursor += size;
  }
}

int load_patch_to_var(const string& name, int var_addr, int cursor) {
  int size = copy_wad_lump_to_elvm(name, cursor);
  write_elvm_word(var_addr, cursor);
  return cursor + size;
}

void process_bfio_status_patches(const vector<byte>& args) {
  int storage = read_u24_arg(args, 0);
  int tallnum = read_u24_arg(args, 1);
  int shortnum = read_u24_arg(args, 2);
  int tallpercent = read_u24_arg(args, 3);
  int keys = read_u24_arg(args, 4);
  int armsbg = read_u24_arg(args, 5);
  int arms = read_u24_arg(args, 6);
  int faceback = read_u24_arg(args, 7);
  int sbar = read_u24_arg(args, 8);
  int faces = read_u24_arg(args, 9);
  int consoleplayer = read_u24_arg(args, 10);
  int cursor = storage;
  int facenum = 0;

  for (int i = 0; i < 10; i++) {
    cursor = load_patch_to_var(numbered_lump_name("STTNUM", i, 1), tallnum + i, cursor);
    cursor = load_patch_to_var(numbered_lump_name("STYSNUM", i, 1), shortnum + i, cursor);
  }

  cursor = load_patch_to_var("STTPRCNT", tallpercent, cursor);

  for (int i = 0; i < 6; i++)
    cursor = load_patch_to_var(numbered_lump_name("STKEYS", i, 1), keys + i, cursor);

  cursor = load_patch_to_var("STARMS", armsbg, cursor);

  for (int i = 0; i < 6; i++) {
    cursor = load_patch_to_var(numbered_lump_name("STGNUM", i + 2, 1), arms + i * 2, cursor);
    write_elvm_word(arms + i * 2 + 1, read_elvm_word_raw(shortnum + i + 2));
  }

  cursor = load_patch_to_var(numbered_lump_name("STFB", consoleplayer, 1), faceback, cursor);
  cursor = load_patch_to_var("STBAR", sbar, cursor);

  for (int i = 0; i < 5; i++) {
    for (int j = 0; j < 3; j++)
      cursor = load_patch_to_var(string("STFST") + char('0' + i) + char('0' + j),
                                 faces + facenum++, cursor);

    cursor = load_patch_to_var(string("STFTR") + char('0' + i) + "0", faces + facenum++, cursor);
    cursor = load_patch_to_var(string("STFTL") + char('0' + i) + "0", faces + facenum++, cursor);
    cursor = load_patch_to_var(string("STFOUCH") + char('0' + i), faces + facenum++, cursor);
    cursor = load_patch_to_var(string("STFEVL") + char('0' + i), faces + facenum++, cursor);
    cursor = load_patch_to_var(string("STFKILL") + char('0' + i), faces + facenum++, cursor);
  }

  cursor = load_patch_to_var("STFGOD0", faces + facenum++, cursor);
  cursor = load_patch_to_var("STFDEAD0", faces + facenum++, cursor);
}

void process_bfio_map_vertexes(const vector<byte>& args) {
  int lump = read_u24_arg(args, 0);
  int vertexes = read_u24_arg(args, 1);
  int count = read_u24_arg(args, 2);
  int vertex_stride = read_u24_arg(args, 3);
  int x_off = read_u24_arg(args, 4);
  int y_off = read_u24_arg(args, 5);

  if (lump < 0 || lump >= (int)g_wad_positions.size())
    return;

  int pos = g_wad_positions[lump];
  for (int i = 0; i < count; i++) {
    int rec = pos + i * 4;
    int vertex = vertexes + i * vertex_stride;
    write_elvm_word(vertex + x_off, fixed_from_i16(wad_le16s(rec)));
    write_elvm_word(vertex + y_off, fixed_from_i16(wad_le16s(rec + 2)));
  }
}

void process_bfio_map_sectors(const vector<byte>& args) {
  int lump = read_u24_arg(args, 0);
  int sectors = read_u24_arg(args, 1);
  int count = read_u24_arg(args, 2);
  int firstflat = read_u24_arg(args, 3);
  int stride = read_u24_arg(args, 4);
  int floorheight_off = read_u24_arg(args, 5);
  int ceilingheight_off = read_u24_arg(args, 6);
  int floorpic_off = read_u24_arg(args, 7);
  int ceilingpic_off = read_u24_arg(args, 8);
  int lightlevel_off = read_u24_arg(args, 9);
  int special_off = read_u24_arg(args, 10);
  int tag_off = read_u24_arg(args, 11);
  int thinglist_off = read_u24_arg(args, 12);

  if (lump < 0 || lump >= (int)g_wad_positions.size())
    return;

  int pos = g_wad_positions[lump];
  int available = g_wad_sizes[lump] / 26;
  if (count > available)
    count = available;

  for (int i = 0; i < count; i++) {
    int rec = pos + i * 26;
    int sector = sectors + i * stride;
    int floor_lump = find_lump_index(read_wad_name(rec + 4, 8));
    int ceiling_lump = find_lump_index(read_wad_name(rec + 12, 8));

    write_elvm_word(sector + floorheight_off, fixed_from_i16(wad_le16s(rec)));
    write_elvm_word(sector + ceilingheight_off,
                    fixed_from_i16(wad_le16s(rec + 2)));
    write_elvm_word(sector + floorpic_off,
                    floor_lump >= 0 ? floor_lump - firstflat : -1);
    write_elvm_word(sector + ceilingpic_off,
                    ceiling_lump >= 0 ? ceiling_lump - firstflat : -1);
    write_elvm_word(sector + lightlevel_off, wad_le16s(rec + 20));
    write_elvm_word(sector + special_off, wad_le16s(rec + 22));
    write_elvm_word(sector + tag_off, wad_le16s(rec + 24));
    write_elvm_word(sector + thinglist_off, 0);
  }
}

void process_bfio_map_sides(const vector<byte>& args) {
  int lump = read_u24_arg(args, 0);
  int sides = read_u24_arg(args, 1);
  int count = read_u24_arg(args, 2);
  int sectors = read_u24_arg(args, 3);
  int sector_stride = read_u24_arg(args, 4);
  int side_stride = read_u24_arg(args, 5);
  int textureoffset_off = read_u24_arg(args, 6);
  int rowoffset_off = read_u24_arg(args, 7);
  int toptexture_off = read_u24_arg(args, 8);
  int bottomtexture_off = read_u24_arg(args, 9);
  int midtexture_off = read_u24_arg(args, 10);
  int sector_off = read_u24_arg(args, 11);

  if (lump < 0 || lump >= (int)g_wad_positions.size())
    return;

  int pos = g_wad_positions[lump];
  int available = g_wad_sizes[lump] / 30;
  if (count > available)
    count = available;

  for (int i = 0; i < count; i++) {
    int rec = pos + i * 30;
    int side = sides + i * side_stride;
    int sector_index = wad_le16s(rec + 28);

    write_elvm_word(side + textureoffset_off, fixed_from_i16(wad_le16s(rec)));
    write_elvm_word(side + rowoffset_off, fixed_from_i16(wad_le16s(rec + 2)));
    write_elvm_word(side + toptexture_off, find_texture_index(read_wad_name(rec + 4, 8)));
    write_elvm_word(side + bottomtexture_off, find_texture_index(read_wad_name(rec + 12, 8)));
    write_elvm_word(side + midtexture_off, find_texture_index(read_wad_name(rec + 20, 8)));
    write_elvm_word(side + sector_off, sectors + sector_index * sector_stride);
  }
}

void process_bfio_map_lines(const vector<byte>& args) {
  int lump = read_u24_arg(args, 0);
  int lines = read_u24_arg(args, 1);
  int count = read_u24_arg(args, 2);
  int vertexes = read_u24_arg(args, 3);
  int vertex_stride = read_u24_arg(args, 4);
  int vertex_x_off = read_u24_arg(args, 5);
  int vertex_y_off = read_u24_arg(args, 6);
  int sides = read_u24_arg(args, 7);
  int side_stride = read_u24_arg(args, 8);
  int side_sector_off = read_u24_arg(args, 9);
  int line_stride = read_u24_arg(args, 10);
  int v1_off = read_u24_arg(args, 11);
  int v2_off = read_u24_arg(args, 12);
  int dx_off = read_u24_arg(args, 13);
  int dy_off = read_u24_arg(args, 14);
  int flags_off = read_u24_arg(args, 15);
  int special_off = read_u24_arg(args, 16);
  int tag_off = read_u24_arg(args, 17);
  int sidenum_off = read_u24_arg(args, 18);
  int bbox_off = read_u24_arg(args, 19);
  int slopetype_off = read_u24_arg(args, 20);
  int frontsector_off = read_u24_arg(args, 21);
  int backsector_off = read_u24_arg(args, 22);

  if (lump < 0 || lump >= (int)g_wad_positions.size())
    return;

  int pos = g_wad_positions[lump];
  int available = g_wad_sizes[lump] / 14;
  if (count > available)
    count = available;

  for (int i = 0; i < count; i++) {
    int rec = pos + i * 14;
    int line = lines + i * line_stride;
    int v1_index = g_wad[rec] | (g_wad[rec + 1] << 8);
    int v2_index = g_wad[rec + 2] | (g_wad[rec + 3] << 8);
    int v1 = vertexes + v1_index * vertex_stride;
    int v2 = vertexes + v2_index * vertex_stride;
    int x1 = read_elvm_word(v1 + vertex_x_off);
    int y1 = read_elvm_word(v1 + vertex_y_off);
    int x2 = read_elvm_word(v2 + vertex_x_off);
    int y2 = read_elvm_word(v2 + vertex_y_off);
    int dx = x2 - x1;
    int dy = y2 - y1;
    int sidenum0 = wad_le16s(rec + 10);
    int sidenum1 = wad_le16s(rec + 12);
    int frontsector = sidenum0 != -1
                          ? read_elvm_word_raw(sides + sidenum0 * side_stride + side_sector_off)
                          : 0;
    int backsector = sidenum1 != -1
                         ? read_elvm_word_raw(sides + sidenum1 * side_stride + side_sector_off)
                         : 0;
    int slopetype;

    if (dx == 0)
      slopetype = 1;
    else if (dy == 0)
      slopetype = 0;
    else if ((dx > 0 && dy > 0) || (dx < 0 && dy < 0))
      slopetype = 2;
    else
      slopetype = 3;

    write_elvm_word(line + flags_off, wad_le16s(rec + 4));
    write_elvm_word(line + special_off, wad_le16s(rec + 6));
    write_elvm_word(line + tag_off, wad_le16s(rec + 8));
    write_elvm_word(line + v1_off, v1);
    write_elvm_word(line + v2_off, v2);
    write_elvm_word(line + dx_off, dx);
    write_elvm_word(line + dy_off, dy);
    write_elvm_word(line + bbox_off + 2, x1 < x2 ? x1 : x2);
    write_elvm_word(line + bbox_off + 3, x1 < x2 ? x2 : x1);
    write_elvm_word(line + bbox_off + 1, y1 < y2 ? y1 : y2);
    write_elvm_word(line + bbox_off, y1 < y2 ? y2 : y1);
    write_elvm_word(line + sidenum_off, sidenum0);
    write_elvm_word(line + sidenum_off + 1, sidenum1);
    write_elvm_word(line + slopetype_off, slopetype);
    write_elvm_word(line + frontsector_off, frontsector);
    write_elvm_word(line + backsector_off, backsector);
  }
}

void process_bfio_map_nodes(const vector<byte>& args) {
  int lump = read_u24_arg(args, 0);
  int nodes = read_u24_arg(args, 1);
  int count = read_u24_arg(args, 2);
  int node_stride = read_u24_arg(args, 3);
  int x_off = read_u24_arg(args, 4);
  int y_off = read_u24_arg(args, 5);
  int dx_off = read_u24_arg(args, 6);
  int dy_off = read_u24_arg(args, 7);
  int bbox_off = read_u24_arg(args, 8);
  int children_off = read_u24_arg(args, 9);

  if (lump < 0 || lump >= (int)g_wad_positions.size())
    return;

  int pos = g_wad_positions[lump];
  int available = g_wad_sizes[lump] / 28;
  if (count > available)
    count = available;

  for (int i = 0; i < count; i++) {
    int rec = pos + i * 28;
    int node = nodes + i * node_stride;

    write_elvm_word(node + x_off, fixed_from_i16(wad_le16s(rec)));
    write_elvm_word(node + y_off, fixed_from_i16(wad_le16s(rec + 2)));
    write_elvm_word(node + dx_off, fixed_from_i16(wad_le16s(rec + 4)));
    write_elvm_word(node + dy_off, fixed_from_i16(wad_le16s(rec + 6)));

    for (int j = 0; j < 2; j++) {
      write_elvm_word(node + children_off + j, g_wad[rec + 24 + j * 2] |
                                                (g_wad[rec + 25 + j * 2] << 8));
      for (int k = 0; k < 4; k++) {
        write_elvm_word(node + bbox_off + j * 4 + k,
                        fixed_from_i16(wad_le16s(rec + 8 + (j * 4 + k) * 2)));
      }
    }
  }
}

void process_bfio_map_segs(const vector<byte>& args) {
  int lump = read_u24_arg(args, 0);
  int segs = read_u24_arg(args, 1);
  int count = read_u24_arg(args, 2);
  int vertexes = read_u24_arg(args, 3);
  int vertex_stride = read_u24_arg(args, 4);
  int sides = read_u24_arg(args, 5);
  int side_stride = read_u24_arg(args, 6);
  int side_sector_off = read_u24_arg(args, 7);
  int numsides = read_u24_arg(args, 8);
  int lines = read_u24_arg(args, 9);
  int line_stride = read_u24_arg(args, 10);
  int line_flags_off = read_u24_arg(args, 11);
  int line_sidenum_off = read_u24_arg(args, 12);
  int seg_stride = read_u24_arg(args, 13);
  int v1_off = read_u24_arg(args, 14);
  int v2_off = read_u24_arg(args, 15);
  int offset_off = read_u24_arg(args, 16);
  int angle_off = read_u24_arg(args, 17);
  int sidedef_off = read_u24_arg(args, 18);
  int linedef_off = read_u24_arg(args, 19);
  int frontsector_off = read_u24_arg(args, 20);
  int backsector_off = read_u24_arg(args, 21);

  if (lump < 0 || lump >= (int)g_wad_positions.size())
    return;

  int pos = g_wad_positions[lump];
  int available = g_wad_sizes[lump] / 12;
  if (count > available)
    count = available;

  for (int i = 0; i < count; i++) {
    int rec = pos + i * 12;
    int seg = segs + i * seg_stride;
    int linedef_index = g_wad[rec + 6] | (g_wad[rec + 7] << 8);
    int side = wad_le16s(rec + 8);
    int line = lines + linedef_index * line_stride;
    int flags = read_elvm_word(line + line_flags_off);
    int sidenum = read_elvm_word(line + line_sidenum_off + side);
    int sidedef = sides + sidenum * side_stride;
    int frontsector = read_elvm_word_raw(sidedef + side_sector_off);
    int backsector = 0;

    if (flags & 4) {
      int back_sidenum = read_elvm_word(line + line_sidenum_off + (side ^ 1));
      if (back_sidenum >= 0 && back_sidenum < numsides) {
        backsector = read_elvm_word_raw(sides + back_sidenum * side_stride +
                                        side_sector_off);
      }
    }

    write_elvm_word(seg + v1_off,
                    vertexes + (g_wad[rec] | (g_wad[rec + 1] << 8)) *
                                   vertex_stride);
    write_elvm_word(seg + v2_off,
                    vertexes + (g_wad[rec + 2] | (g_wad[rec + 3] << 8)) *
                                   vertex_stride);
    write_elvm_word(seg + angle_off, fixed_from_i16(wad_le16s(rec + 4)));
    write_elvm_word(seg + offset_off, fixed_from_i16(wad_le16s(rec + 10)));
    write_elvm_word(seg + linedef_off, line);
    write_elvm_word(seg + sidedef_off, sidedef);
    write_elvm_word(seg + frontsector_off, frontsector);
    write_elvm_word(seg + backsector_off, backsector);
  }
}

static int clamp_int(int value, int lo, int hi) {
  if (value < lo)
    return lo;
  if (value > hi)
    return hi;
  return value;
}

static int host_point_on_side(int x, int y, int node, int x_off, int y_off,
                              int dx_off, int dy_off) {
  int node_x = read_elvm_word(node + x_off);
  int node_y = read_elvm_word(node + y_off);
  int node_dx = read_elvm_word(node + dx_off);
  int node_dy = read_elvm_word(node + dy_off);

  if (node_dx == 0) {
    if (x <= node_x)
      return node_dy > 0;
    return node_dy < 0;
  }

  if (node_dy == 0) {
    if (y <= node_y)
      return node_dx < 0;
    return node_dx > 0;
  }

  int point_dx = x - node_x;
  int point_dy = y - node_y;
  long long left = ((long long)(node_dy >> 16) * point_dx) >> 16;
  long long right = ((long long)point_dy * (node_dx >> 16)) >> 16;

  return right < left ? 0 : 1;
}

void process_bfio_player_start(const vector<byte>& args) {
  int lump = read_u24_arg(args, 0);
  int field = sign24(read_u24_arg(args, 1));

  int found = -1;
  if (lump >= 0 && lump < (int)g_wad_positions.size()) {
    int pos = g_wad_positions[lump];
    int count = g_wad_sizes[lump] / 10;
    for (int i = 0; i < count; i++) {
      int rec = pos + i * 10;
      if (wad_le16s(rec + 6) == 1) {
        found = rec;
        break;
      }
    }
  }

  if (found < 0) {
    enqueue_u24(0);
    return;
  }

  static const int kOffsets[] = {0, 2, 4, 6, 8};
  int value = 0;
  if (field >= 0 && field < 5)
    value = wad_le16s(found + kOffsets[field]);
  enqueue_u24(value);
  g_host_actors_ready = false;
}

void process_bfio_group_lines(const vector<byte>& args) {
  int sectors = read_u24_arg(args, 0);
  int numsectors = read_u24_arg(args, 1);
  int subsectors = read_u24_arg(args, 2);
  int numsubsectors = read_u24_arg(args, 3);
  int segs = read_u24_arg(args, 4);
  int seg_stride = read_u24_arg(args, 5);
  int seg_frontsector_off = read_u24_arg(args, 6);
  int lines = read_u24_arg(args, 7);
  int numlines = read_u24_arg(args, 8);
  int line_stride = read_u24_arg(args, 9);
  int line_frontsector_off = read_u24_arg(args, 10);
  int line_backsector_off = read_u24_arg(args, 11);
  int line_bbox_off = read_u24_arg(args, 12);
  int linebuffer = read_u24_arg(args, 13);
  int totallines_out = read_u24_arg(args, 14);
  int sector_stride = read_u24_arg(args, 15);
  int sector_linecount_off = read_u24_arg(args, 16);
  int sector_lines_off = read_u24_arg(args, 17);
  int sector_blockbox_off = read_u24_arg(args, 18);
  int sector_soundorg_x_off = read_u24_arg(args, 19);
  int sector_soundorg_y_off = read_u24_arg(args, 20);
  int subsector_stride = read_u24_arg(args, 21);
  int subsector_firstline_off = read_u24_arg(args, 22);
  int subsector_sector_off = read_u24_arg(args, 23);
  int bmaporgx_value = sign24(read_u24_arg(args, 24));
  int bmaporgy_value = sign24(read_u24_arg(args, 25));
  int bmapwidth_value = sign24(read_u24_arg(args, 26));
  int bmapheight_value = sign24(read_u24_arg(args, 27));
  int maxradius_value = sign24(read_u24_arg(args, 28));
  int mapblockshift_value = sign24(read_u24_arg(args, 29));
  int total = 0;

  for (int i = 0; i < numsubsectors; i++) {
    int subsector = subsectors + i * subsector_stride;
    int firstline = read_elvm_word(subsector + subsector_firstline_off);
    int seg = segs + firstline * seg_stride;
    write_elvm_word(subsector + subsector_sector_off,
                    read_elvm_word_raw(seg + seg_frontsector_off));
  }

  for (int i = 0; i < numsectors; i++)
    write_elvm_word(sectors + i * sector_stride + sector_linecount_off, 0);

  for (int i = 0; i < numlines; i++) {
    int line = lines + i * line_stride;
    int front = read_elvm_word_raw(line + line_frontsector_off);
    int back = read_elvm_word_raw(line + line_backsector_off);

    total++;
    if (front)
      write_elvm_word(front + sector_linecount_off,
                      read_elvm_word(front + sector_linecount_off) + 1);
    if (back && back != front) {
      total++;
      write_elvm_word(back + sector_linecount_off,
                      read_elvm_word(back + sector_linecount_off) + 1);
    }
  }

  int cursor = linebuffer;
  for (int i = 0; i < numsectors; i++) {
    int sector = sectors + i * sector_stride;
    int count = read_elvm_word(sector + sector_linecount_off);
    write_elvm_word(sector + sector_lines_off, cursor);
    cursor += count;
    write_elvm_word(sector + sector_linecount_off, 0);
  }

  for (int i = 0; i < numlines; i++) {
    int line = lines + i * line_stride;
    int front = read_elvm_word_raw(line + line_frontsector_off);
    int back = read_elvm_word_raw(line + line_backsector_off);

    if (front) {
      int count = read_elvm_word(front + sector_linecount_off);
      int list = read_elvm_word_raw(front + sector_lines_off);
      write_elvm_word(list + count, line);
      write_elvm_word(front + sector_linecount_off, count + 1);
    }

    if (back && back != front) {
      int count = read_elvm_word(back + sector_linecount_off);
      int list = read_elvm_word_raw(back + sector_lines_off);
      write_elvm_word(list + count, line);
      write_elvm_word(back + sector_linecount_off, count + 1);
    }
  }

  for (int i = 0; i < numsectors; i++) {
    int sector = sectors + i * sector_stride;
    int count = read_elvm_word(sector + sector_linecount_off);
    int list = read_elvm_word_raw(sector + sector_lines_off);
    int top = -0x800000;
    int bottom = 0x7fffff;
    int left = 0x7fffff;
    int right = -0x800000;

    for (int j = 0; j < count; j++) {
      int line = read_elvm_word_raw(list + j);
      int ltop = read_elvm_word(line + line_bbox_off);
      int lbottom = read_elvm_word(line + line_bbox_off + 1);
      int lleft = read_elvm_word(line + line_bbox_off + 2);
      int lright = read_elvm_word(line + line_bbox_off + 3);

      if (ltop > top)
        top = ltop;
      if (lbottom < bottom)
        bottom = lbottom;
      if (lleft < left)
        left = lleft;
      if (lright > right)
        right = lright;
    }

    write_elvm_word(sector + sector_soundorg_x_off, (right + left) / 2);
    write_elvm_word(sector + sector_soundorg_y_off, (top + bottom) / 2);

    int block = (top - bmaporgy_value + maxradius_value) >> mapblockshift_value;
    write_elvm_word(sector + sector_blockbox_off,
                    clamp_int(block, 0, bmapheight_value - 1));
    block = (bottom - bmaporgy_value - maxradius_value) >> mapblockshift_value;
    write_elvm_word(sector + sector_blockbox_off + 1, clamp_int(block, 0, bmapheight_value - 1));
    block = (right - bmaporgx_value + maxradius_value) >> mapblockshift_value;
    write_elvm_word(sector + sector_blockbox_off + 3, clamp_int(block, 0, bmapwidth_value - 1));
    block = (left - bmaporgx_value - maxradius_value) >> mapblockshift_value;
    write_elvm_word(sector + sector_blockbox_off + 2, clamp_int(block, 0, bmapwidth_value - 1));
  }

  write_elvm_word(totallines_out, total);
}

void process_bfio_point_in_subsector(const vector<byte>& args) {
  int x = sign24(read_u24_arg(args, 0));
  int y = sign24(read_u24_arg(args, 1));
  int nodes = read_u24_arg(args, 2);
  int numnodes = read_u24_arg(args, 3);
  int node_stride = read_u24_arg(args, 4);
  int node_x_off = read_u24_arg(args, 5);
  int node_y_off = read_u24_arg(args, 6);
  int node_dx_off = read_u24_arg(args, 7);
  int node_dy_off = read_u24_arg(args, 8);
  int node_children_off = read_u24_arg(args, 9);
  int subsectors = read_u24_arg(args, 10);
  int subsector_stride = read_u24_arg(args, 11);
  int nf_subsector = read_u24_arg(args, 12);

  if (!numnodes) {
    enqueue_u24(subsectors);
    return;
  }

  int nodenum = numnodes - 1;
  while (!(nodenum & nf_subsector)) {
    int node = nodes + nodenum * node_stride;
    int side = host_point_on_side(x, y, node, node_x_off, node_y_off,
                                  node_dx_off, node_dy_off);
    nodenum = read_elvm_word_raw(node + node_children_off + side);
  }

  enqueue_u24(subsectors + (nodenum & ~nf_subsector) * subsector_stride);
}

void process_bfio_mobj_type(const vector<byte>& args) {
  int doomednum = sign24(read_u24_arg(args, 0));
  int mobjinfo = read_u24_arg(args, 1);
  int count = read_u24_arg(args, 2);
  int stride = read_u24_arg(args, 3);
  int doomednum_off = read_u24_arg(args, 4);

  for (int i = 0; i < count; i++) {
    if (read_elvm_word(mobjinfo + i * stride + doomednum_off) == doomednum) {
      enqueue_u24(i + 1);
      return;
    }
  }

  enqueue_u24(0);
}

void process_bfio_z_malloc(const vector<byte>& args) {
  int zone = read_u24_arg(args, 0);
  int size = sign24(read_u24_arg(args, 1));
  int tag = sign24(read_u24_arg(args, 2));
  int user = read_u24_arg(args, 3);
  int mem_align = sign24(read_u24_arg(args, 4));
  int memblock_size = sign24(read_u24_arg(args, 5));
  int minfragment = sign24(read_u24_arg(args, 6));
  int zoneid = sign24(read_u24_arg(args, 7));
  int pu_free = sign24(read_u24_arg(args, 8));
  int pu_purgelevel = sign24(read_u24_arg(args, 9));
  int zone_rover_off = read_u24_arg(args, 10);
  int block_size_off = read_u24_arg(args, 11);
  int block_user_off = read_u24_arg(args, 12);
  int block_tag_off = read_u24_arg(args, 13);
  int block_id_off = read_u24_arg(args, 14);
  int block_next_off = read_u24_arg(args, 15);
  int block_prev_off = read_u24_arg(args, 16);

  auto block_tag = [&](int block) {
    return read_elvm_word(block + block_tag_off);
  };
  auto block_next = [&](int block) {
    return read_elvm_word_raw(block + block_next_off);
  };
  auto block_prev = [&](int block) {
    return read_elvm_word_raw(block + block_prev_off);
  };
  auto host_free = [&](int ptr) {
    int block = ptr - memblock_size;
    int user_ptr = read_elvm_word_raw(block + block_user_off);

    if (block_tag(block) != pu_free && user_ptr)
      write_elvm_word(user_ptr, 0);

    write_elvm_word(block + block_tag_off, pu_free);
    write_elvm_word(block + block_user_off, 0);
    write_elvm_word(block + block_id_off, 0);

    int other = block_prev(block);
    if (block_tag(other) == pu_free) {
      write_elvm_word(other + block_size_off,
                      read_elvm_word(other + block_size_off) +
                          read_elvm_word(block + block_size_off));
      write_elvm_word(other + block_next_off, block_next(block));
      write_elvm_word(block_next(block) + block_prev_off, other);
      if (block == read_elvm_word_raw(zone + zone_rover_off))
        write_elvm_word(zone + zone_rover_off, other);
      block = other;
    }

    other = block_next(block);
    if (block_tag(other) == pu_free) {
      write_elvm_word(block + block_size_off,
                      read_elvm_word(block + block_size_off) +
                          read_elvm_word(other + block_size_off));
      write_elvm_word(block + block_next_off, block_next(other));
      write_elvm_word(block_next(other) + block_prev_off, block);
      if (other == read_elvm_word_raw(zone + zone_rover_off))
        write_elvm_word(zone + zone_rover_off, block);
    }
  };

  size = (size + mem_align - 1) & ~(mem_align - 1);
  size += memblock_size;

  int base = read_elvm_word_raw(zone + zone_rover_off);
  if (block_tag(block_prev(base)) == pu_free)
    base = block_prev(base);

  int rover = base;
  int start = block_prev(base);
  int guard = 0;

  do {
    if (rover == start || guard++ > 100000) {
      enqueue_u24(0);
      return;
    }

    if (block_tag(rover) != pu_free) {
      if (block_tag(rover) < pu_purgelevel) {
        base = block_next(rover);
        rover = base;
      } else {
        base = block_prev(base);
        host_free(rover + memblock_size);
        base = block_next(base);
        rover = block_next(base);
      }
    } else {
      rover = block_next(rover);
    }
  } while (block_tag(base) != pu_free || read_elvm_word(base + block_size_off) < size);

  int extra = read_elvm_word(base + block_size_off) - size;
  if (extra > minfragment) {
    int newblock = base + size;
    write_elvm_word(newblock + block_size_off, extra);
    write_elvm_word(newblock + block_tag_off, pu_free);
    write_elvm_word(newblock + block_user_off, 0);
    write_elvm_word(newblock + block_prev_off, base);
    write_elvm_word(newblock + block_next_off, block_next(base));
    write_elvm_word(block_next(base) + block_prev_off, newblock);
    write_elvm_word(base + block_next_off, newblock);
    write_elvm_word(base + block_size_off, size);
  }

  if (user == 0 && tag >= pu_purgelevel) {
    enqueue_u24(0);
    return;
  }

  write_elvm_word(base + block_user_off, user);
  write_elvm_word(base + block_tag_off, tag);

  int result = base + memblock_size;
  if (user)
    write_elvm_word(user, result);

  write_elvm_word(zone + zone_rover_off, block_next(base));
  write_elvm_word(base + block_id_off, zoneid);
  enqueue_u24(result);
}

int host_point_in_subsector_addr(int x, int y, int nodes, int numnodes,
                                 int node_stride, int node_x_off,
                                 int node_y_off, int node_dx_off,
                                 int node_dy_off, int node_children_off,
                                 int subsectors, int subsector_stride,
                                 int nf_subsector) {
  if (!numnodes)
    return subsectors;

  int nodenum = numnodes - 1;
  while (!(nodenum & nf_subsector)) {
    int node = nodes + nodenum * node_stride;
    int side = host_point_on_side(x, y, node, node_x_off, node_y_off,
                                  node_dx_off, node_dy_off);
    nodenum = read_elvm_word_raw(node + node_children_off + side);
  }

  return subsectors + (nodenum & ~nf_subsector) * subsector_stride;
}

double host_cross(double ax, double ay, double bx, double by) {
  return ax * by - ay * bx;
}

bool host_segments_intersect(double ax, double ay, double bx, double by,
                             double cx, double cy, double dx, double dy) {
  double rx = bx - ax;
  double ry = by - ay;
  double sx = dx - cx;
  double sy = dy - cy;
  double denom = host_cross(rx, ry, sx, sy);
  if (fabs(denom) < 0.000001)
    return false;

  double qpx = cx - ax;
  double qpy = cy - ay;
  double t = host_cross(qpx, qpy, sx, sy) / denom;
  double u = host_cross(qpx, qpy, rx, ry) / denom;
  return t > 0.0 && t <= 1.0 && u >= 0.0 && u <= 1.0;
}

bool host_line_blocked_by_map(double ax, double ay, double bx, double by) {
  ensure_host_map();
  for (int i = 0; i < (int)g_host_lines.size(); i++) {
    if (!g_host_lines[i].solid)
      continue;
    if (host_segments_intersect(ax, ay, bx, by, g_host_lines[i].x1,
                                g_host_lines[i].y1, g_host_lines[i].x2,
                                g_host_lines[i].y2))
      return true;
  }
  return false;
}

bool host_path_blocked_by_line_map(double ax, double ay, double bx, double by) {
  ensure_host_map();
  for (int i = 0; i < (int)g_host_lines.size(); i++) {
    if (!g_host_lines[i].blocks_move)
      continue;
    if (host_segments_intersect(ax, ay, bx, by, g_host_lines[i].x1,
                                g_host_lines[i].y1, g_host_lines[i].x2,
                                g_host_lines[i].y2))
      return true;
  }
  return false;
}

void host_collect_pickups() {
  ensure_host_actors();
  ensure_host_player_start();
  for (size_t i = 0; i < g_host_actors.size(); i++) {
    HostActor& actor = g_host_actors[i];
    if (!actor.alive || host_actor_is_enemy(actor.type))
      continue;

    double dx = actor.x - g_host_player_x;
    double dy = actor.y - g_host_player_y;
    if (dx * dx + dy * dy > 34.0 * 34.0)
      continue;
    if (host_line_blocked_by_map(g_host_player_x, g_host_player_y,
                                 actor.x, actor.y))
      continue;

    if (host_actor_is_weapon(actor.type)) {
      int slot = host_weapon_slot_from_actor(actor.type);
      if (slot > 0 && slot < 8) {
        g_host_weapon_owned[slot] = true;
        g_host_weapon = slot;
      }
      host_add_weapon_ammo(actor.type);
    } else if (host_actor_is_ammo(actor.type)) {
      host_add_pickup_ammo(actor.type);
    } else if (host_actor_is_health(actor.type)) {
      int cap = actor.type == 2013 ? 200 : 100;
      int gain = actor.type == 2011 ? 10 : actor.type == 2012 ? 25 : 1;
      g_host_health += gain;
      if (g_host_health > cap)
        g_host_health = cap;
    } else if (host_actor_is_armor(actor.type)) {
      int value = actor.type == 2019 ? 200 : actor.type == 2018 ? 100 : 1;
      if (actor.type == 2015) {
        g_host_armor += value;
        if (g_host_armor > 200)
          g_host_armor = 200;
      } else if (g_host_armor < value) {
        g_host_armor = value;
      }
    } else {
      continue;
    }

    actor.alive = false;
    g_host_pickup_flash = 5;
    if (!isatty(STDERR_FILENO)) {
      fprintf(stderr, "pickup type=%d health=%d armor=%d ammo=%d\n",
              actor.type, g_host_health, g_host_armor, host_current_ammo());
    }
  }
}

void host_use_action() {
  ensure_host_map();
  ensure_host_player_start();
  double rdx = cos(g_host_player_angle);
  double rdy = sin(g_host_player_angle);
  int best = -1;
  double best_dist = 1.0e30;

  for (int i = 0; i < (int)g_host_lines.size(); i++) {
    HostLine& line = g_host_lines[i];
    if (!line.blocks_move || (!line.door && !line.exit))
      continue;
    double sdx = line.x2 - line.x1;
    double sdy = line.y2 - line.y1;
    double denom = rdx * sdy - rdy * sdx;
    if (fabs(denom) < 0.000001)
      continue;
    double qx = line.x1 - g_host_player_x;
    double qy = line.y1 - g_host_player_y;
    double t = (qx * sdy - qy * sdx) / denom;
    double u = (qx * rdy - qy * rdx) / denom;
    if (t > 1.0 && t < 96.0 && u >= 0.0 && u <= 1.0 && t < best_dist) {
      best = i;
      best_dist = t;
    }
  }

  if (best >= 0) {
    if (g_host_lines[best].exit) {
      bool advanced = host_advance_map();
      if (!advanced)
        g_host_level_complete = true;
      g_host_pickup_flash = 8;
      if (!isatty(STDERR_FILENO)) {
        fprintf(stderr, "exit activated_line=%d map=%s advanced=%d\n", best,
                g_host_map_name.c_str(), advanced ? 1 : 0);
      }
      return;
    }

    int tag = g_host_lines[best].tag;
    if (tag > 0) {
      for (int i = 0; i < (int)g_host_lines.size(); i++) {
        if (g_host_lines[i].tag == tag && g_host_lines[i].door) {
          g_host_lines[i].solid = false;
          g_host_lines[i].blocks_move = false;
        }
      }
    } else {
      g_host_lines[best].solid = false;
      g_host_lines[best].blocks_move = false;
    }
    g_host_pickup_flash = 3;
    if (!isatty(STDERR_FILENO))
      fprintf(stderr, "use opened_line=%d tag=%d\n", best, tag);
  }
}

void host_alert_enemies_from_sound() {
  ensure_host_actors();
  ensure_host_player_start();
  int sound_wakes = 0;
  int ambush_sight_wakes = 0;
  for (size_t i = 0; i < g_host_actors.size(); i++) {
    HostActor& actor = g_host_actors[i];
    if (!actor.alive || actor.awake || !host_actor_is_enemy(actor.type))
      continue;

    double dx = g_host_player_x - actor.x;
    double dy = g_host_player_y - actor.y;
    double dist = sqrt(dx * dx + dy * dy);
    if (dist > 4096.0)
      continue;

    bool line_of_sight = !host_line_blocked_by_map(actor.x, actor.y,
                                                   g_host_player_x,
                                                   g_host_player_y);
    if (actor.ambush) {
      if (!line_of_sight)
        continue;
      host_wake_actor(actor);
      ambush_sight_wakes++;
    } else {
      host_wake_actor(actor);
      sound_wakes++;
    }
  }
  if ((sound_wakes > 0 || ambush_sight_wakes > 0) && !isatty(STDERR_FILENO)) {
    fprintf(stderr, "alert sound=%d ambush_sight=%d\n", sound_wakes,
            ambush_sight_wakes);
  }
}

bool host_fire_hitscan_shot(double angle,
                            int damage,
                            double max_dist,
                            bool use_rendered_crosshair,
                            bool log_miss) {
  double px = g_host_player_x;
  double py = g_host_player_y;
  double aim_dx = cos(angle);
  double aim_dy = sin(angle);

  int best = -1;
  double best_dist = 1.0e30;
  int nearest = -1;
  double nearest_score = 1.0e30;
  double nearest_dist = 0.0;
  double nearest_rel = 0.0;

  int cross_x = g_host_render_width > 0 ? g_host_render_width / 2 : kFrameWidth / 2;
  if (use_rendered_crosshair) {
    for (size_t i = 0; i < g_host_rendered_actors.size(); i++) {
      const HostRenderedActor& rendered = g_host_rendered_actors[i];
      if (rendered.index < 0 || rendered.index >= (int)g_host_actors.size())
        continue;
      HostActor& actor = g_host_actors[rendered.index];
      if (!actor.alive || !host_actor_is_enemy(actor.type))
        continue;
      if (rendered.dist > max_dist)
        continue;
      if (cross_x < rendered.x0 - 8 || cross_x > rendered.x1 + 8)
        continue;
      if (rendered.dist < best_dist) {
        best = rendered.index;
        best_dist = rendered.dist;
      }
    }
  }

  for (size_t i = 0; i < g_host_actors.size(); i++) {
    HostActor& actor = g_host_actors[i];
    if (!actor.alive || !host_actor_is_enemy(actor.type))
      continue;

    double dx = actor.x - px;
    double dy = actor.y - py;
    double dist = sqrt(dx * dx + dy * dy);
    double along = dx * aim_dx + dy * aim_dy;
    if (along <= 0.0 || along > max_dist)
      continue;
    double lateral = fabs(dx * aim_dy - dy * aim_dx);
    double radius = host_actor_radius(actor.type);
    double tolerance = tan(host_weapon_aim_width(dist)) * dist;
    if (tolerance < radius)
      tolerance = radius;
    double hit_x = px + aim_dx * along;
    double hit_y = py + aim_dy * along;
    double rel = normalize_host_angle(atan2(dy, dx) - angle);
    bool clear = !host_line_blocked_by_map(px, py, hit_x, hit_y);
    if (clear && fabs(rel) < nearest_score) {
      nearest = (int)i;
      nearest_score = fabs(rel);
      nearest_dist = dist;
      nearest_rel = rel;
    }
    if (lateral <= tolerance && along < best_dist && clear) {
      best = (int)i;
      best_dist = along;
    }
  }

  if (best >= 0) {
    host_wake_actor(g_host_actors[best]);
    g_host_actors[best].health -= damage;
    g_host_actors[best].flash = 3;
    if (g_host_actors[best].health <= 0) {
      g_host_actors[best].alive = false;
      g_host_actors[best].flash = 0;
      g_host_actors[best].attack_tics = 0;
      g_host_actors[best].death_tics = 1;
    }
    if (!isatty(STDERR_FILENO)) {
      fprintf(stderr, "hit type=%d damage=%d health=%d alive=%d\n",
              g_host_actors[best].type, damage, g_host_actors[best].health,
              g_host_actors[best].alive ? 1 : 0);
    }
    return true;
  } else if (log_miss && !isatty(STDERR_FILENO)) {
    if (nearest >= 0) {
      fprintf(stderr,
              "miss rendered=%zu nearest_type=%d dist=%.1f rel_deg=%.1f aim_deg=%.1f pos=(%.1f,%.1f) angle=%.1f\n",
              g_host_rendered_actors.size(), g_host_actors[nearest].type,
              nearest_dist, nearest_rel * 180.0 / kPi,
              host_weapon_aim_width(nearest_dist) * 180.0 / kPi, px, py,
              angle * 180.0 / kPi);
    } else {
      fprintf(stderr,
              "miss rendered=%zu nearest_type=0 pos=(%.1f,%.1f) angle=%.1f\n",
              g_host_rendered_actors.size(), px, py, angle * 180.0 / kPi);
    }
  }
  return false;
}

void host_fire_hitscan(const vector<byte>& render_args) {
  (void)render_args;
  ensure_host_actors();
  ensure_host_player_start();

  double max_dist = g_host_weapon == 1 ? 65.0 : 2048.0;

  if (g_host_weapon == 3) {
    bool any_hit = false;
    for (int i = 0; i < 7; i++) {
      int damage = host_bullet_damage();
      double angle = normalize_host_angle(g_host_player_angle +
                                          host_doom_angle_spread(18));
      bool hit = host_fire_hitscan_shot(angle, damage, max_dist, false,
                                        i == 6 && !any_hit);
      any_hit = any_hit || hit;
    }
    return;
  }

  if (g_host_weapon == 1) {
    double angle = normalize_host_angle(g_host_player_angle +
                                        host_doom_angle_spread(18));
    host_fire_hitscan_shot(angle, host_melee_damage(), max_dist, false, true);
    return;
  }

  bool accurate = g_host_weapon == 2 || g_host_weapon == 4;
  double angle = g_host_player_angle;
  if (!accurate)
    angle = normalize_host_angle(angle + host_doom_angle_spread(18));
  host_fire_hitscan_shot(angle, host_weapon_damage(), max_dist, accurate, true);
}

void process_bfio_set_thing_position(const vector<byte>& args) {
  int thing = read_u24_arg(args, 0);
  int nodes = read_u24_arg(args, 1);
  int numnodes = read_u24_arg(args, 2);
  int node_stride = read_u24_arg(args, 3);
  int node_x_off = read_u24_arg(args, 4);
  int node_y_off = read_u24_arg(args, 5);
  int node_dx_off = read_u24_arg(args, 6);
  int node_dy_off = read_u24_arg(args, 7);
  int node_children_off = read_u24_arg(args, 8);
  int subsectors = read_u24_arg(args, 9);
  int subsector_stride = read_u24_arg(args, 10);
  int subsector_sector_off = read_u24_arg(args, 11);
  int blocklinks = read_u24_arg(args, 12);
  int bmaporgx_value = sign24(read_u24_arg(args, 13));
  int bmaporgy_value = sign24(read_u24_arg(args, 14));
  int bmapwidth_value = sign24(read_u24_arg(args, 15));
  int bmapheight_value = sign24(read_u24_arg(args, 16));
  int mapblockshift_value = sign24(read_u24_arg(args, 17));
  int mf_nosector = sign24(read_u24_arg(args, 18));
  int mf_noblockmap = sign24(read_u24_arg(args, 19));
  int thing_x_off = read_u24_arg(args, 20);
  int thing_y_off = read_u24_arg(args, 21);
  int thing_flags_off = read_u24_arg(args, 22);
  int thing_subsector_off = read_u24_arg(args, 23);
  int thing_snext_off = read_u24_arg(args, 24);
  int thing_sprev_off = read_u24_arg(args, 25);
  int thing_bnext_off = read_u24_arg(args, 26);
  int thing_bprev_off = read_u24_arg(args, 27);
  int sector_thinglist_off = read_u24_arg(args, 28);
  int nf_subsector = read_u24_arg(args, 29);
  int x = read_elvm_word(thing + thing_x_off);
  int y = read_elvm_word(thing + thing_y_off);
  int flags = read_elvm_word(thing + thing_flags_off);
  int ss = host_point_in_subsector_addr(x, y, nodes, numnodes, node_stride,
                                        node_x_off, node_y_off, node_dx_off,
                                        node_dy_off, node_children_off,
                                        subsectors, subsector_stride,
                                        nf_subsector);

  write_elvm_word(thing + thing_subsector_off, ss);

  if (!(flags & mf_nosector)) {
    int sec = read_elvm_word_raw(ss + subsector_sector_off);
    int head = read_elvm_word_raw(sec + sector_thinglist_off);
    write_elvm_word(thing + thing_sprev_off, 0);
    write_elvm_word(thing + thing_snext_off, head);
    if (head)
      write_elvm_word(head + thing_sprev_off, thing);
    write_elvm_word(sec + sector_thinglist_off, thing);
  }

  if (!(flags & mf_noblockmap)) {
    int blockx = (x - bmaporgx_value) >> mapblockshift_value;
    int blocky = (y - bmaporgy_value) >> mapblockshift_value;

    if (blockx >= 0 && blockx < bmapwidth_value && blocky >= 0 &&
        blocky < bmapheight_value) {
      int link = blocklinks + blocky * bmapwidth_value + blockx;
      int head = read_elvm_word_raw(link);
      write_elvm_word(thing + thing_bprev_off, 0);
      write_elvm_word(thing + thing_bnext_off, head);
      if (head)
        write_elvm_word(head + thing_bprev_off, thing);
      write_elvm_word(link, thing);
    } else {
      write_elvm_word(thing + thing_bnext_off, 0);
      write_elvm_word(thing + thing_bprev_off, 0);
    }
  }
}

void process_bfio_spawn_mobj(const vector<byte>& args) {
  int mobj = read_u24_arg(args, 0);
  int mobj_size = sign24(read_u24_arg(args, 1));
  int x = sign24(read_u24_arg(args, 2));
  int y = sign24(read_u24_arg(args, 3));
  int z = sign24(read_u24_arg(args, 4));
  int type = sign24(read_u24_arg(args, 5));
  int mobjinfo = read_u24_arg(args, 6);
  int info_stride = read_u24_arg(args, 7);
  int info_spawnstate_off = read_u24_arg(args, 8);
  int info_spawnhealth_off = read_u24_arg(args, 9);
  int info_reactiontime_off = read_u24_arg(args, 10);
  int info_radius_off = read_u24_arg(args, 11);
  int info_height_off = read_u24_arg(args, 12);
  int info_flags_off = read_u24_arg(args, 13);
  int states = read_u24_arg(args, 14);
  int state_stride = read_u24_arg(args, 15);
  int state_sprite_off = read_u24_arg(args, 16);
  int state_frame_off = read_u24_arg(args, 17);
  int state_tics_off = read_u24_arg(args, 18);
  int nodes = read_u24_arg(args, 19);
  int numnodes = read_u24_arg(args, 20);
  int node_stride = read_u24_arg(args, 21);
  int node_x_off = read_u24_arg(args, 22);
  int node_y_off = read_u24_arg(args, 23);
  int node_dx_off = read_u24_arg(args, 24);
  int node_dy_off = read_u24_arg(args, 25);
  int node_children_off = read_u24_arg(args, 26);
  int subsectors = read_u24_arg(args, 27);
  int subsector_stride = read_u24_arg(args, 28);
  int subsector_sector_off = read_u24_arg(args, 29);
  int blocklinks = read_u24_arg(args, 30);
  int bmaporgx_value = sign24(read_u24_arg(args, 31));
  int bmaporgy_value = sign24(read_u24_arg(args, 32));
  int bmapwidth_value = sign24(read_u24_arg(args, 33));
  int bmapheight_value = sign24(read_u24_arg(args, 34));
  int mapblockshift_value = sign24(read_u24_arg(args, 35));
  int mf_nosector = sign24(read_u24_arg(args, 36));
  int mf_noblockmap = sign24(read_u24_arg(args, 37));
  int nf_subsector = read_u24_arg(args, 38);
  int mobj_x_off = read_u24_arg(args, 39);
  int mobj_y_off = read_u24_arg(args, 40);
  int mobj_z_off = read_u24_arg(args, 41);
  int mobj_type_off = read_u24_arg(args, 42);
  int mobj_info_off = read_u24_arg(args, 43);
  int mobj_radius_off = read_u24_arg(args, 44);
  int mobj_height_off = read_u24_arg(args, 45);
  int mobj_flags_off = read_u24_arg(args, 46);
  int mobj_health_off = read_u24_arg(args, 47);
  int mobj_reactiontime_off = read_u24_arg(args, 48);
  int mobj_lastlook_off = read_u24_arg(args, 49);
  int mobj_state_off = read_u24_arg(args, 50);
  int mobj_tics_off = read_u24_arg(args, 51);
  int mobj_sprite_off = read_u24_arg(args, 52);
  int mobj_frame_off = read_u24_arg(args, 53);
  int mobj_subsector_off = read_u24_arg(args, 54);
  int mobj_floorz_off = read_u24_arg(args, 55);
  int mobj_ceilingz_off = read_u24_arg(args, 56);
  int mobj_snext_off = read_u24_arg(args, 57);
  int mobj_sprev_off = read_u24_arg(args, 58);
  int mobj_bnext_off = read_u24_arg(args, 59);
  int mobj_bprev_off = read_u24_arg(args, 60);
  int sector_thinglist_off = read_u24_arg(args, 61);
  int sector_floorheight_off = read_u24_arg(args, 62);
  int sector_ceilingheight_off = read_u24_arg(args, 63);
  int onfloorz = sign24(read_u24_arg(args, 64));
  int onceilingz = sign24(read_u24_arg(args, 65));

  for (int i = 0; i < mobj_size; i++)
    write_elvm_word(mobj + i, 0);

  int info = mobjinfo + type * info_stride;
  int spawnstate = read_elvm_word(info + info_spawnstate_off);
  int st = states + spawnstate * state_stride;
  int flags = read_elvm_word(info + info_flags_off);

  write_elvm_word(mobj + mobj_type_off, type);
  write_elvm_word(mobj + mobj_info_off, info);
  write_elvm_word(mobj + mobj_x_off, x);
  write_elvm_word(mobj + mobj_y_off, y);
  write_elvm_word(mobj + mobj_radius_off, read_elvm_word(info + info_radius_off));
  write_elvm_word(mobj + mobj_height_off, read_elvm_word(info + info_height_off));
  write_elvm_word(mobj + mobj_flags_off, flags);
  write_elvm_word(mobj + mobj_health_off, read_elvm_word(info + info_spawnhealth_off));
  write_elvm_word(mobj + mobj_reactiontime_off, read_elvm_word(info + info_reactiontime_off));
  write_elvm_word(mobj + mobj_lastlook_off, 0);
  write_elvm_word(mobj + mobj_state_off, st);
  write_elvm_word(mobj + mobj_tics_off, read_elvm_word(st + state_tics_off));
  write_elvm_word(mobj + mobj_sprite_off, read_elvm_word(st + state_sprite_off));
  write_elvm_word(mobj + mobj_frame_off, read_elvm_word(st + state_frame_off));

  int ss = host_point_in_subsector_addr(x, y, nodes, numnodes, node_stride,
                                        node_x_off, node_y_off, node_dx_off,
                                        node_dy_off, node_children_off,
                                        subsectors, subsector_stride,
                                        nf_subsector);
  write_elvm_word(mobj + mobj_subsector_off, ss);

  if (!(flags & mf_nosector)) {
    int sec = read_elvm_word_raw(ss + subsector_sector_off);
    int head = read_elvm_word_raw(sec + sector_thinglist_off);
    write_elvm_word(mobj + mobj_sprev_off, 0);
    write_elvm_word(mobj + mobj_snext_off, head);
    if (head)
      write_elvm_word(head + mobj_sprev_off, mobj);
    write_elvm_word(sec + sector_thinglist_off, mobj);
  }

  if (!(flags & mf_noblockmap)) {
    int blockx = (x - bmaporgx_value) >> mapblockshift_value;
    int blocky = (y - bmaporgy_value) >> mapblockshift_value;
    if (blockx >= 0 && blockx < bmapwidth_value && blocky >= 0 &&
        blocky < bmapheight_value) {
      int link = blocklinks + blocky * bmapwidth_value + blockx;
      int head = read_elvm_word_raw(link);
      write_elvm_word(mobj + mobj_bprev_off, 0);
      write_elvm_word(mobj + mobj_bnext_off, head);
      if (head)
        write_elvm_word(head + mobj_bprev_off, mobj);
      write_elvm_word(link, mobj);
    }
  }

  int sector = read_elvm_word_raw(ss + subsector_sector_off);
  int floorz = read_elvm_word(sector + sector_floorheight_off);
  int ceilingz = read_elvm_word(sector + sector_ceilingheight_off);
  write_elvm_word(mobj + mobj_floorz_off, floorz);
  write_elvm_word(mobj + mobj_ceilingz_off, ceilingz);

  if (z == onfloorz)
    write_elvm_word(mobj + mobj_z_off, floorz);
  else if (z == onceilingz)
    write_elvm_word(mobj + mobj_z_off, ceilingz - read_elvm_word(info + info_height_off));
  else
    write_elvm_word(mobj + mobj_z_off, z);
}

void process_bfio_size(const string& path) {
  enqueue_u24(is_host_wad_path(path) ? (int)g_wad.size() : 0);
}

void process_bfio_lump_name(const string& name) {
  string key = normalize_lump_name(name);
  int i = find_lump_index(name);
  if (i >= 0) {
    if (g_trace)
      fprintf(stderr, "BFIO lump %s -> %d\n", key.c_str(), i);
    enqueue_u24(i + 1);
    return;
  }
  if (g_trace)
    fprintf(stderr, "BFIO lump %s -> -1\n", key.c_str());
  enqueue_u24(0);
}

void process_bfio_texture_index(const string& name) {
  string key = normalize_lump_name(name);
  int i = find_texture_index(name);

  if (i >= 0) {
    if (g_trace)
      fprintf(stderr, "BFIO texture %s -> %d\n", key.c_str(), i);
    enqueue_u24(i + 1);
    return;
  }

  if (g_trace)
    fprintf(stderr, "BFIO texture %s -> -1\n", key.c_str());
  enqueue_u24(0);
}

void process_bfio_lump_present(const string& name) {
  string key = normalize_lump_name(name);
  int i = find_lump_index(name);
  if (g_trace)
    fprintf(stderr, "BFIO present %s -> %d\n", key.c_str(), i >= 0 ? 1 : 0);
  enqueue_bfio_byte(i >= 0 ? 1 : 0);
}

void process_bfio_read(const vector<byte>& args) {
  int offset = args[0] | (args[1] << 8) | (args[2] << 16);
  int len = args[3] | (args[4] << 8) | (args[5] << 16);
  enqueue_read_response(offset, len);
}

void doom_host_putchar(byte b) {
  enum State {
    NORMAL,
    BFIO_CMD,
    BFIO_PATH,
    BFIO_LUMPNAME,
    BFIO_TEXTURENAME,
    BFIO_PRESENT,
    BFIO_READ,
    BFIO_DMA,
    BFIO_LUMPDIR,
    BFIO_PATCHHEADERS,
    BFIO_PATCHLOOKUP,
    BFIO_TEXTURELOOKUP,
    BFIO_LOADTEXTURES,
    BFIO_FILLWORDS,
    BFIO_LIGHTTABLES,
    BFIO_VIEWTABLES,
    BFIO_TRANSLATIONTABLES,
    BFIO_RENDERVIEW,
    BFIO_SPRITEPREP,
    BFIO_SPRITEFILL,
    BFIO_DRAWFRAME,
    BFIO_HUFONTS,
    BFIO_STATUSPATCHES,
    BFIO_MAPVERTEXES,
    BFIO_MAPSECTORS,
    BFIO_MAPSIDES,
    BFIO_MAPLINES,
    BFIO_MAPNODES,
    BFIO_MAPSEGS,
    BFIO_PLAYERSTART,
    BFIO_GROUPLINES,
    BFIO_POINTSUBSECTOR,
    BFIO_MOBJTYPE,
    BFIO_ZMALLOC,
    BFIO_SETTHINGPOS,
    BFIO_SPAWNMOBJ,
    FRAME,
  };
  static State state = NORMAL;
  static int pending_kind = 0;
  static size_t pending_index = 0;
  static vector<byte> pending;
  static string path;
  static vector<byte> args;
  static vector<byte> frame;

  const byte bfio_magic[] = {255, 0, 'B', 'F', 'I', 'O'};
  const byte frame_magic[] = {'B', 'F', 'G', '2'};

  if (state == BFIO_CMD) {
    if (b == 'S') {
      path.clear();
      state = BFIO_PATH;
    } else if (b == 'N') {
      path.clear();
      state = BFIO_LUMPNAME;
    } else if (b == 'T') {
      path.clear();
      state = BFIO_TEXTURENAME;
    } else if (b == 'P') {
      path.clear();
      state = BFIO_PRESENT;
    } else if (b == 'R') {
      args.clear();
      state = BFIO_READ;
    } else if (b == 'D') {
      args.clear();
      state = BFIO_DMA;
    } else if (b == 'L') {
      args.clear();
      state = BFIO_LUMPDIR;
    } else if (b == 'H') {
      args.clear();
      state = BFIO_PATCHHEADERS;
    } else if (b == 'J') {
      args.clear();
      state = BFIO_PATCHLOOKUP;
    } else if (b == 'G') {
      args.clear();
      state = BFIO_TEXTURELOOKUP;
    } else if (b == 't') {
      args.clear();
      state = BFIO_LOADTEXTURES;
    } else if (b == 'F') {
      args.clear();
      state = BFIO_FILLWORDS;
    } else if (b == 'l') {
      args.clear();
      state = BFIO_LIGHTTABLES;
    } else if (b == 'w') {
      args.clear();
      state = BFIO_VIEWTABLES;
    } else if (b == 'r') {
      args.clear();
      state = BFIO_TRANSLATIONTABLES;
    } else if (b == 'o') {
      args.clear();
      state = BFIO_RENDERVIEW;
    } else if (b == 'Y') {
      args.clear();
      state = BFIO_SPRITEPREP;
    } else if (b == 'Z') {
      args.clear();
      state = BFIO_SPRITEFILL;
    } else if (b == 'Q') {
      args.clear();
      state = BFIO_DRAWFRAME;
    } else if (b == 'U') {
      args.clear();
      state = BFIO_HUFONTS;
    } else if (b == 'O') {
      args.clear();
      state = BFIO_STATUSPATCHES;
    } else if (b == 'v') {
      args.clear();
      state = BFIO_MAPVERTEXES;
    } else if (b == 'A') {
      args.clear();
      state = BFIO_MAPSECTORS;
    } else if (b == 'B') {
      args.clear();
      state = BFIO_MAPSIDES;
    } else if (b == 'C') {
      args.clear();
      state = BFIO_MAPLINES;
    } else if (b == 'E') {
      args.clear();
      state = BFIO_MAPNODES;
    } else if (b == 'I') {
      args.clear();
      state = BFIO_MAPSEGS;
    } else if (b == 'n') {
      args.clear();
      state = BFIO_PLAYERSTART;
    } else if (b == 'V') {
      args.clear();
      state = BFIO_GROUPLINES;
    } else if (b == 'X') {
      args.clear();
      state = BFIO_POINTSUBSECTOR;
    } else if (b == 'M') {
      args.clear();
      state = BFIO_MOBJTYPE;
    } else if (b == 'z') {
      args.clear();
      state = BFIO_ZMALLOC;
    } else if (b == 'p') {
      args.clear();
      state = BFIO_SETTHINGPOS;
    } else if (b == 'm') {
      args.clear();
      state = BFIO_SPAWNMOBJ;
    } else if (b == 'K') {
      enqueue_key_poll();
      state = NORMAL;
    } else {
      state = NORMAL;
    }
    return;
  }

  if (state == BFIO_PATH) {
    if (b == 0) {
      process_bfio_size(path);
      state = NORMAL;
    } else {
      path.push_back((char)b);
    }
    return;
  }

  if (state == BFIO_LUMPNAME) {
    if (b == 0) {
      process_bfio_lump_name(path);
      state = NORMAL;
    } else {
      path.push_back((char)b);
    }
    return;
  }

  if (state == BFIO_TEXTURENAME) {
    if (b == 0) {
      process_bfio_texture_index(path);
      state = NORMAL;
    } else {
      path.push_back((char)b);
    }
    return;
  }

  if (state == BFIO_PRESENT) {
    if (b == 0) {
      process_bfio_lump_present(path);
      state = NORMAL;
    } else {
      path.push_back((char)b);
    }
    return;
  }

  if (state == BFIO_READ) {
    args.push_back(b);
    if (args.size() == 6) {
      process_bfio_read(args);
      state = NORMAL;
    }
    return;
  }

  if (state == BFIO_DMA) {
    args.push_back(b);
    if (args.size() == 9) {
      process_bfio_dma(args);
      state = NORMAL;
    }
    return;
  }

  if (state == BFIO_LUMPDIR) {
    args.push_back(b);
    if (args.size() == 30) {
      process_bfio_lumpdir(args);
      state = NORMAL;
    }
    return;
  }

  if (state == BFIO_PATCHHEADERS) {
    args.push_back(b);
    if (args.size() == 15) {
      process_bfio_patch_headers(args);
      state = NORMAL;
    }
    return;
  }

  if (state == BFIO_PATCHLOOKUP) {
    args.push_back(b);
    if (args.size() == 6) {
      process_bfio_patch_lookup(args);
      state = NORMAL;
    }
    return;
  }

  if (state == BFIO_TEXTURELOOKUP) {
    args.push_back(b);
    if (args.size() == 39) {
      process_bfio_texture_lookup(args);
      state = NORMAL;
    }
    return;
  }

  if (state == BFIO_LOADTEXTURES) {
    args.push_back(b);
    if (args.size() == 75) {
      process_bfio_load_textures(args);
      state = NORMAL;
    }
    return;
  }

  if (state == BFIO_FILLWORDS) {
    args.push_back(b);
    if (args.size() == 9) {
      process_bfio_fill_words(args);
      state = NORMAL;
    }
    return;
  }

  if (state == BFIO_LIGHTTABLES) {
    args.push_back(b);
    if (args.size() == 15) {
      process_bfio_light_tables(args);
      state = NORMAL;
    }
    return;
  }

  if (state == BFIO_VIEWTABLES) {
    args.push_back(b);
    if (args.size() == 72) {
      process_bfio_view_tables(args);
      state = NORMAL;
    }
    return;
  }

  if (state == BFIO_TRANSLATIONTABLES) {
    args.push_back(b);
    if (args.size() == 3) {
      process_bfio_translation_tables(args);
      state = NORMAL;
    }
    return;
  }

  if (state == BFIO_RENDERVIEW) {
    args.push_back(b);
    if (args.size() == 45) {
      process_bfio_render_map_view(args);
      state = NORMAL;
    }
    return;
  }

  if (state == BFIO_SPRITEPREP) {
    args.push_back(b);
    if (args.size() == 18) {
      process_bfio_prepare_sprite_defs(args);
      state = NORMAL;
    }
    return;
  }

  if (state == BFIO_SPRITEFILL) {
    args.push_back(b);
    if (args.size() == 24) {
      process_bfio_fill_sprite_defs(args);
      state = NORMAL;
    }
    return;
  }

  if (state == BFIO_DRAWFRAME) {
    args.push_back(b);
    if (args.size() == 27) {
      process_bfio_draw_frame(args);
      state = NORMAL;
    }
    return;
  }

  if (state == BFIO_HUFONTS) {
    args.push_back(b);
    if (args.size() == 12) {
      process_bfio_hu_fonts(args);
      state = NORMAL;
    }
    return;
  }

  if (state == BFIO_STATUSPATCHES) {
    args.push_back(b);
    if (args.size() == 33) {
      process_bfio_status_patches(args);
      state = NORMAL;
    }
    return;
  }

  if (state == BFIO_MAPVERTEXES) {
    args.push_back(b);
    if (args.size() == 18) {
      process_bfio_map_vertexes(args);
      state = NORMAL;
    }
    return;
  }

  if (state == BFIO_MAPSECTORS) {
    args.push_back(b);
    if (args.size() == 39) {
      process_bfio_map_sectors(args);
      state = NORMAL;
    }
    return;
  }

  if (state == BFIO_MAPSIDES) {
    args.push_back(b);
    if (args.size() == 36) {
      process_bfio_map_sides(args);
      state = NORMAL;
    }
    return;
  }

  if (state == BFIO_MAPLINES) {
    args.push_back(b);
    if (args.size() == 69) {
      process_bfio_map_lines(args);
      state = NORMAL;
    }
    return;
  }

  if (state == BFIO_MAPNODES) {
    args.push_back(b);
    if (args.size() == 30) {
      process_bfio_map_nodes(args);
      state = NORMAL;
    }
    return;
  }

  if (state == BFIO_MAPSEGS) {
    args.push_back(b);
    if (args.size() == 66) {
      process_bfio_map_segs(args);
      state = NORMAL;
    }
    return;
  }

  if (state == BFIO_PLAYERSTART) {
    args.push_back(b);
    if (args.size() == 6) {
      process_bfio_player_start(args);
      state = NORMAL;
    }
    return;
  }

  if (state == BFIO_GROUPLINES) {
    args.push_back(b);
    if (args.size() == 90) {
      process_bfio_group_lines(args);
      state = NORMAL;
    }
    return;
  }

  if (state == BFIO_POINTSUBSECTOR) {
    args.push_back(b);
    if (args.size() == 39) {
      process_bfio_point_in_subsector(args);
      state = NORMAL;
    }
    return;
  }

  if (state == BFIO_MOBJTYPE) {
    args.push_back(b);
    if (args.size() == 15) {
      process_bfio_mobj_type(args);
      state = NORMAL;
    }
    return;
  }

  if (state == BFIO_ZMALLOC) {
    args.push_back(b);
    if (args.size() == 51) {
      process_bfio_z_malloc(args);
      state = NORMAL;
    }
    return;
  }

  if (state == BFIO_SETTHINGPOS) {
    args.push_back(b);
    if (args.size() == 90) {
      process_bfio_set_thing_position(args);
      state = NORMAL;
    }
    return;
  }

  if (state == BFIO_SPAWNMOBJ) {
    args.push_back(b);
    if (args.size() == 198) {
      process_bfio_spawn_mobj(args);
      state = NORMAL;
    }
    return;
  }

  if (state == FRAME) {
    frame.push_back(b);
    if (frame.size() == kFrameBytes) {
      handle_frame(frame);
      frame.clear();
      state = NORMAL;
    }
    return;
  }

retry_normal:
  if (pending_kind == 0) {
    if (b == bfio_magic[0]) {
      pending_kind = 1;
      pending_index = 1;
      pending.clear();
      pending.push_back(b);
      return;
    }
    if (b == frame_magic[0]) {
      pending_kind = 2;
      pending_index = 1;
      pending.clear();
      pending.push_back(b);
      return;
    }
    fputc(b, stderr);
    fflush(stderr);
    return;
  }

  const byte* magic = pending_kind == 1 ? bfio_magic : frame_magic;
  size_t magic_len = pending_kind == 1 ? sizeof(bfio_magic) : sizeof(frame_magic);

  if (b == magic[pending_index]) {
    pending.push_back(b);
    pending_index++;
    if (pending_index == magic_len) {
      pending_kind = 0;
      pending_index = 0;
      pending.clear();
      if (magic_len == sizeof(bfio_magic)) {
        state = BFIO_CMD;
      } else {
        frame.clear();
        frame.reserve(kFrameBytes);
        state = FRAME;
      }
    }
    return;
  }

  for (size_t i = 0; i < pending.size(); i++) {
    if (pending[i] >= 32 || pending[i] == '\n' || pending[i] == '\r' || pending[i] == '\t')
      fputc(pending[i], stderr);
  }
  pending_kind = 0;
  pending_index = 0;
  pending.clear();
  goto retry_normal;
}

int doom_host_getchar() {
  if (g_input_queue.empty())
    return 0;

  int c = g_input_queue.front();
  g_input_queue.pop_front();
  return c;
}

int translate_snapshot_input(int ch) {
  if (ch != 27)
    return ch;

  int next = read_host_stdin_byte();
  if (next == -1) {
    usleep(1000);
    next = read_host_stdin_byte();
  }
  if (next != '[')
    return 27;

  int arrow = read_host_stdin_byte();
  if (arrow == -1) {
    usleep(1000);
    arrow = read_host_stdin_byte();
  }

  switch (arrow) {
    case 'A':
      return 'w';
    case 'B':
      return 's';
    case 'C':
      return 'd';
    case 'D':
      return 'a';
    default:
      return 0;
  }
}

void run_snapshot_host_loop(vector<byte>* mem) {
  if (g_last_render_args.empty() || g_last_draw_args.empty())
    return;

  g_active_mem = mem;
  ensure_host_player_start();
  process_bfio_render_map_view(g_last_render_args);
  process_bfio_draw_frame(g_last_draw_args);

  auto apply_turn = [](int dir) {
    if (dir < 0)
      g_host_player_angle = normalize_host_angle(g_host_player_angle - 0.13);
    else if (dir > 0)
      g_host_player_angle = normalize_host_angle(g_host_player_angle + 0.13);
  };

  auto apply_move = [](int dir) {
    double nx = g_host_player_x + cos(g_host_player_angle) * 16.0 * dir;
    double ny = g_host_player_y + sin(g_host_player_angle) * 16.0 * dir;
    if (!host_path_blocked_by_line_map(g_host_player_x, g_host_player_y, nx,
                                       ny)) {
      g_host_player_x = nx;
      g_host_player_y = ny;
    }
  };

  auto handle_input = [](int ch) -> bool {
    if (ch == -2)
      return false;
    if (ch == 'q' || ch == 'Q' || ch == 27)
      return false;
    if ('1' <= ch && ch <= '7') {
      int slot = ch - '0';
      if (g_host_weapon_owned[slot]) {
        g_host_weapon = slot;
        if (!isatty(STDERR_FILENO))
          fprintf(stderr, "weapon slot=%d\n", slot);
      }
    }
    if (ch == ' ' || ch == 'f' || ch == 'F') {
      int cost = host_weapon_ammo_cost();
      int ammo_type = host_weapon_ammo_type();
      if (ammo_type < 0 || g_host_ammo[ammo_type] >= cost) {
        if (ammo_type >= 0)
          g_host_ammo[ammo_type] -= cost;
        g_host_fire_flash = 3;
        if (!isatty(STDERR_FILENO)) {
          fprintf(stderr, "fire weapon=%d ammo=%d\n", g_host_weapon,
                  host_current_ammo());
        }
        host_alert_enemies_from_sound();
        host_fire_hitscan(g_last_render_args);
      }
    }
    if (ch == 'e' || ch == 'E')
      host_use_action();
    if (ch == 'a' || ch == 'A')
      g_host_player_angle = normalize_host_angle(g_host_player_angle - 0.13);
    if (ch == 'd' || ch == 'D')
      g_host_player_angle = normalize_host_angle(g_host_player_angle + 0.13);
    if (ch == 'w' || ch == 'W' || ch == 's' || ch == 'S') {
      int dir = (ch == 's' || ch == 'S') ? -1 : 1;
      double nx = g_host_player_x + cos(g_host_player_angle) * 16.0 * dir;
      double ny = g_host_player_y + sin(g_host_player_angle) * 16.0 * dir;
      if (!host_path_blocked_by_line_map(g_host_player_x, g_host_player_y,
                                         nx, ny)) {
        g_host_player_x = nx;
        g_host_player_y = ny;
      }
    }
    return true;
  };

  const bool interactive_input = isatty(STDIN_FILENO);
  int held_turn = 0;
  int held_move = 0;
  int turn_ttl = 0;
  int move_ttl = 0;

  while (true) {
    bool should_quit = false;
    int input_limit = interactive_input ? 128 : 64;
    for (int i = 0; i < input_limit; i++) {
      int ch = translate_snapshot_input(read_host_stdin_byte());
      if (ch == -1)
        break;
      if (interactive_input && (ch == 'a' || ch == 'A' || ch == 'd' ||
                                ch == 'D')) {
        held_turn = (ch == 'a' || ch == 'A') ? -1 : 1;
        turn_ttl = 5;
        continue;
      }
      if (interactive_input && (ch == 'w' || ch == 'W' || ch == 's' ||
                                ch == 'S')) {
        held_move = (ch == 's' || ch == 'S') ? -1 : 1;
        move_ttl = 5;
        continue;
      }
      if (!handle_input(ch)) {
        should_quit = true;
        break;
      }
    }

    if (interactive_input) {
      if (turn_ttl > 0) {
        apply_turn(held_turn);
        turn_ttl--;
      }
      if (move_ttl > 0) {
        apply_move(held_move);
        move_ttl--;
      }
    }

    process_bfio_render_map_view(g_last_render_args);
    process_bfio_draw_frame(g_last_draw_args);
    if (should_quit)
      return;
    usleep(20000);
  }
}

void run_fast(const vector<FastOp>& fast, const vector<LoopTerm>& loop_terms) {
  int mp = 0;
  vector<byte> mem(4096 * 4096 * 10);
  size_t start_pc = 0;
  load_vm_snapshot(&mem, &mp, &start_pc);
  g_active_mem = &mem;
  if (g_doom_host)
    enable_raw_terminal();
  if (g_snapshot_loaded && !g_last_render_args.empty() &&
      !g_last_draw_args.empty()) {
    if (g_raw_terminal || !isatty(STDIN_FILENO)) {
      run_snapshot_host_loop(&mem);
    } else {
      process_bfio_render_map_view(g_last_render_args);
      process_bfio_draw_frame(g_last_draw_args);
    }
    g_active_mem = NULL;
    disable_raw_terminal();
    return;
  }
  for (size_t pc = start_pc; pc < fast.size(); pc++) {
    const FastOp& op = fast[pc];
    if (g_trace || g_verbose) {
      g_run_pc = pc;
      g_run_op = op.op;
      g_run_arg = op.arg;
    }
    switch (op.op) {
      case '+':
        mem[mp]++;
        break;

      case '-':
        mem[mp]--;
        break;

      case OP_MEM:
        mem[mp] += op.arg;
        break;

      case '>':
        mp++;
        ensure_fast_head(&mp, &mem);
        break;

      case '<':
        mp--;
        ensure_fast_head(&mp, &mem);
        break;

      case OP_PTR:
        mp += op.arg;
        ensure_fast_head(&mp, &mem);
        break;

      case '.':
        if (g_doom_host)
          doom_host_putchar(mem[mp]);
        else
          putchar(mem[mp]);
        break;

      case ',':
        mem[mp] = g_doom_host ? doom_host_getchar() : getchar();
        break;

      case '[':
        if (mem[mp] == 0)
          pc = op.arg;
        break;

      case ']':
        if (mem[mp])
          pc = op.arg;
        break;

      case OP_LOOP: {
        int v = mem[mp];
        mem[mp] = 0;
        for (int i = 0; i < op.aux; i++) {
          const LoopTerm& term = loop_terms[op.arg + i];
          int index = ensure_relative(&mp, term.rel, &mem);
          mem[index] += v * term.delta;
        }
        break;
      }

      case OP_COMMENT: {
        fprintf(stderr, "TRACE %s %f %zu\n",
                op.comment,
                static_cast<double>(clock()) / CLOCKS_PER_SEC,
                pc);
        break;
      }

      case '@':
        if (g_verbose)
          dump_state(mem);
        break;

    }
    if (g_snapshot_due) {
      g_snapshot_due = false;
      save_vm_snapshot(mem, mp, pc + 1);
    }
  }
  g_active_mem = NULL;
  disable_raw_terminal();
  if (g_terminal_started)
    fprintf(stderr, "\033[0m\033[?7h\033[?25h\n");
}

void compile(const vector<Op*>& ops, const char* fname) {
  FILE* fp = fopen(fname, "wb");
  fprintf(fp, "#include <stdio.h>\n");
  fprintf(fp, "unsigned char mem[4096*4096*10];\n");
  fprintf(fp, "int main() {\n");
  fprintf(fp, "unsigned char* mp = mem;\n");

  for (size_t pc = 0; pc < ops.size(); pc++) {
    const Op* op = ops[pc];
    switch (op->op) {
      case '+':
        fprintf(fp, "++*mp;\n");
        break;

      case '-':
        fprintf(fp, "--*mp;\n");
        break;

      case OP_MEM:
        if (op->arg)
          fprintf(fp, "*mp += %d;\n", op->arg);
        break;

      case '>':
        fprintf(fp, "mp++;\n");
        break;

      case '<':
        fprintf(fp, "mp--;\n");
        break;

      case OP_PTR:
        if (op->arg)
          fprintf(fp, "mp += %d;\n", op->arg);
        break;

      case '.':
        fprintf(fp, "putchar(*mp);\n");
        break;

      case ',':
        fprintf(fp, "*mp = getchar();\n");
        break;

      case '[':
        fprintf(fp, "while (*mp) {\n");
        break;

      case ']':
        fprintf(fp, "}\n");
        break;

      case OP_LOOP: {
        for (map<int, int>::const_iterator iter = op->loop->addsub.begin();
             iter != op->loop->addsub.end();
             ++iter) {
          int p = iter->first;
          int d = iter->second;
          if (p != 0) {
            fprintf(fp, "mp[%d] += *mp * %d;\n", p, d);
          }
        }
        fprintf(fp, "*mp = 0;\n");
        break;
      }

    }
  }

  fprintf(fp, "return 0;\n");
  fprintf(fp, "}\n");
  fclose(fp);
}

int main(int argc, char* argv[]) {
  bool should_compile = false;
  const char* arg0 = argv[0];
  while (argc >= 2 && argv[1][0] == '-') {
    if (!strcmp(argv[1], "-c")) {
      should_compile = true;
    } else if (!strcmp(argv[1], "-t")) {
      g_trace = true;
    } else if (!strcmp(argv[1], "-v")) {
      g_verbose = true;
    } else if (!strcmp(argv[1], "-doom-host")) {
      g_doom_host = true;
    } else if (!strcmp(argv[1], "-window-stream")) {
      g_doom_host = true;
      g_window_stream = true;
    } else if (!strcmp(argv[1], "-wad")) {
      if (argc < 3) {
        fprintf(stderr, "-wad requires a path\n");
        return 1;
      }
      g_wad_path = argv[2];
      argc--;
      argv++;
    } else if (!strcmp(argv[1], "-capture")) {
      if (argc < 3) {
        fprintf(stderr, "-capture requires a path\n");
        return 1;
      }
      g_capture_path = argv[2];
      argc--;
      argv++;
    } else {
      fprintf(stderr, "Unknown flag: %s\n", argv[1]);
      return 1;
    }
    argc--;
    argv++;
  }

  if (argc < 2 || (argc < 3 && should_compile)) {
    fprintf(stderr, "Usage: %s <bf>\n", arg0);
    return 1;
  }

  if (g_doom_host && !load_wad(g_wad_path)) {
    fprintf(stderr, "failed to load WAD: %s\n", g_wad_path.c_str());
    return 1;
  }
  if (g_doom_host)
    atexit(disable_raw_terminal);

  const char* fname = argv[1];
  g_program_path = fname;
  if (!should_compile) {
    vector<FastOp> fast;
    vector<LoopTerm> loop_terms;
    if (load_fast_cache(fname, &fast, &loop_terms)) {
      run_fast(fast, loop_terms);
      return 0;
    }
  }

  FILE* fp = fopen(fname, "rb");
  if (!fp) {
    perror("open");
    return 1;
  }
  string buf;
  while (true) {
    int c = fgetc(fp);
    if (c == EOF)
      break;
    if (c == '#') {
      c = fgetc(fp);
      if (c == '{') {
        buf += "#{";
        for (; c != '}';) {
          c = fgetc(fp);
          buf += c;
        }
      }
    }
    if (strchr("+-<>.,[]@", c))
      buf += c;
  }
  fclose(fp);

  vector<Op*> ops;
  parse(buf.c_str(), &ops);
  if (should_compile) {
    compile(ops, argv[2]);
  } else {
    vector<FastOp> fast;
    vector<LoopTerm> loop_terms;
    flatten_ops(ops, &fast, &loop_terms);
    save_fast_cache(fname, fast, loop_terms);
    run_fast(fast, loop_terms);
  }
}
