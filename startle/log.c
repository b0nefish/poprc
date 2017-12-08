/* Copyright 2012-2017 Dustin DeWeese
   This file is part of PoprC.

    PoprC is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    PoprC is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with PoprC.  If not, see <http://www.gnu.org/licenses/>.
*/

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <inttypes.h>
#include <stdbool.h>

#include "startle/types.h"
#include "startle/macros.h"
#include "startle/error.h"
#include "startle/log.h"
#include "startle/support.h"

#define FORMAT_ITEM(name, c) extern FORMAT(name, c);
#include "format_list.h"
#undef FORMAT_ITEM

#define REVERSE 0x80
#define INDENT  0x40
#define MASK (REVERSE | INDENT)

#define LOG_SIZE (1 << 12)
static intptr_t log[LOG_SIZE];
static unsigned int log_head = 0;
static unsigned int log_tail = 0;
static unsigned int log_watch = ~0;
static unsigned int log_watch_to = ~0;
static intptr_t log_watch_fmt = 0;
static bool set_log_watch_fmt = false;
static bool watching = false;
static unsigned int msg_head = 0;

static bool tweak_enabled = false;
static unsigned int tweak_trigger = ~0;
static intptr_t tweak_value = 0;

static uintptr_t hash_tag_set[63];

context_t *__log_context = NULL;

void log_init() {
  log[0] = 0;
  log_head = 0;
  log_tail = 0;
  __log_context = NULL;
  log_watch = ~0;
  log_watch_to = ~0;
  log_watch_fmt = 0;
  set_log_watch_fmt = false;
  watching = false;
  msg_head = 0;
  zero(hash_tag_set);
}

void set_log_watch(const tag_t tag, bool after) {
  log_watch = log_watch_to = read_tag(tag);
  set_log_watch_fmt = after;
  log_watch_fmt = 0;
  watching = false;
}

void set_log_watch_range(const tag_t tag_from, const char *tag_to) {
  log_watch = read_tag(tag_from);
  log_watch_to = tag_to ? read_tag(tag_to) : ~0;
  log_watch_fmt = 0;
  watching = false;
}

void log_soft_init() {
  if(log_head != log_tail) {
    log_add((intptr_t)"\xff\xff"); // reset indentation
  }
  __log_context = NULL;
}

static
int log_entry_len(unsigned int idx) {
  const char *fmt = (const char *)log[idx];
  if(!fmt) return 0;
  char len = fmt[0];
  if(len == '\xff') return 0;
  return (uint8_t)(len & ~MASK);
}

// not the most efficient
static
char *strchrnul(const char *s, int c) {
  char *res = strchr(s, c);
  return res ? res : strchr(s, 0);
}

static
unsigned int log_printf(unsigned int idx, unsigned int *depth, bool event) {
  unsigned int msg_id = idx;
  const char *fmt = (const char *)log[idx++];
  tag_t tag;
  //printf("%d %d %x %s\n", idx, *depth, fmt[0], fmt + 1);
  uint8_t len = fmt[0] & ~MASK;
  intptr_t x;
  const char
    *p = fmt + 1,
    *n = strpbrk(p, "%#@");
  LOOP(*depth * 2) putchar(' ');
  if(fmt[0] & INDENT) (*depth)++;
  while(n) {
    printf("%.*s", (int)(n-p), p); // print the text
    if(!n[1]) break;
    if(n[0] != '%') {
      p = strchrnul(n, ' ');
      uintptr_t key = nonzero_hash(n+1, p-n-1);
      if(n[0] == '@' || set_member(key, hash_tag_set, LENGTH(hash_tag_set))) {
        printf(NOTE("%.*s"), (int)(p-n), n);
      } else {
        printf("%.*s", (int)(p-n), n);
      }
    } else {
      switch(n[1]) {
#define CASE_PRINT(c, print)                    \
        case c:                                 \
          if(len) {                             \
            idx = idx % LOG_SIZE;               \
            x = log[idx++];                     \
            print;                              \
            len--;                              \
          } else {                              \
            printf("X");                        \
          }                                     \
          break;
#define CASE(c, cast, fmt)                      \
        CASE_PRINT(c, printf(fmt, cast(x)))
#define FORMAT_ITEM(name, c) CASE_PRINT(c, format_##name(x))
#include "format_list.h"
#undef FORMAT_ITEM
        CASE('d', (int), "%d");
        CASE('u', (unsigned int), "%u");
        CASE('x', (int), "%x");
        CASE('s', (const char *), "%s");
        CASE('p', (void *), "%p");
#undef CASE
#undef CASE_PRINT
      case '.':
        if(n[2] == '*' && n[3] == 's') {
          if(len > 1) {
            idx = idx % LOG_SIZE;
            int size = log[idx++];
            idx = idx % LOG_SIZE;
            printf("%.*s", size, (const char *)log[idx++]);
            len -= 2;
          } else {
            printf("X");
          }
          n += 2;
          break;
        }
      case '%':
        printf("%%");
        break;
      default:
        printf("!?");
        break;
      }
      p = n + 2;
    }
    n = strpbrk(p, "%#@");
  }
  idx = idx % LOG_SIZE;
  if(event) {
    write_tag(tag, msg_id);
    printf("%s " FADE(FORMAT_TAG) "\n", p, tag);
  } else {
    printf("%s\n", p);
  }
  return idx;
}

void log_add(intptr_t x) {
  log[log_head] = x;
  log_head = (log_head + 1) % LOG_SIZE;
  if(log_head == log_tail) {
    unsigned int len = log_entry_len(log_tail);
    log_tail = (log_tail + 1 + len) % LOG_SIZE;
  }
}

void log_add_first(intptr_t x) {
  msg_head = log_head;
  log_add(x);
}

bool log_add_last(intptr_t x) {
  log_add(x);
  if unlikely(msg_head == log_watch ||
              (watching &&
               (!set_log_watch_fmt ||
                log[msg_head] == log_watch_fmt))) {
    watching = true;
    if(set_log_watch_fmt) {
      log_watch_fmt = log[msg_head];
    } else if(msg_head == log_watch_to) {
      watching = false;
    }
    return true;
  }
  return false;
}

bool log_add_only(intptr_t x) {
  msg_head = log_head;
  return log_add_last(x);
}

void print_last_log_msg() {
  unsigned int depth = 0;
  log_printf(msg_head, &depth, true);
}

static
bool end_context(unsigned int idx, unsigned int *depth) {
  const char *fmt = (const char *)log[idx];
  // TODO match the ends so that dropping entries doesn't break indentation
  if(fmt[0] != '\xff') return false;
  if(fmt[1] == '\xff') {
    *depth = 0;
    putchar('\n');
  } else if(*depth > 0) {
    (*depth)--;
  }
  return true;
}

static
unsigned int print_contexts(unsigned int idx, unsigned int *depth) {
  if(idx == log_head) return idx;
  const char *fmt = (const char *)log[idx];
  if(fmt[0] == '\xff' ||
     (fmt[0] & REVERSE) == 0) return idx;
  uint8_t len = (fmt[0] & ~MASK) + 1;
  unsigned int ret = print_contexts((idx + len) % LOG_SIZE, depth);
  log_printf(idx, depth, false);
  return ret;
}

void log_print_all() {
  log_scan_tags();
  unsigned int
    depth = 0,
    i = log_tail;
  while(i != log_head) {
    i = print_contexts(i, &depth);
    if(i == log_head) break;
    if(end_context(i, &depth)) {
      i = (i + 1) % LOG_SIZE;
    } else {
      i = log_printf(i, &depth, true);
    }
  }
}

void log_scan_tags() {
  zero(hash_tag_set);
  unsigned int i = log_tail;
  while(i != log_head) {
    const char *fmt = (const char *)log[i];
    if(*fmt == '\xff') {
      i++;
      continue;
    }
    uint8_t len = *fmt & ~MASK;
    i += len + 1;
    const char *p = fmt;
    while((p = strchr(p, '@'))) {
      p++;
      const char *e = strchrnul(p, ' ');
      if(p == e) continue;
      uintptr_t key = nonzero_hash(p, e-p);
      set_insert(key, hash_tag_set, LENGTH(hash_tag_set));
      p = e;
    }
  }
}

#if INTERFACE
#define LOG_pre                                 \
  do {                                          \
    log_add_context();
#define LOG_first(s, fmt) log_add_first((intptr_t)(s fmt));
#define LOG_middle(x) log_add((intptr_t)(x));
#define LOG_last(x) if(log_add_last((intptr_t)(x))) breakpoint();
#define LOG_only(s, fmt) if(log_add_only((intptr_t)(s fmt))) breakpoint();
#define LOG_post                                \
  } while(0)
#define LOG_args ("\x00", "\x01", "\x02", "\x03", "\x04", "\x05", "\x06", "\x07", "\x08")
#define LOG_NO_POS(...) FORARG(LOG, __VA_ARGS__)
#define LOG(fmt, ...) LOG_NO_POS(__FILE__ ":" STRINGIFY(__LINE__) ": " fmt, ##__VA_ARGS__)
#define LOG_WHEN(test, fmt, ...) ((test) && (({ LOG(fmt, ##__VA_ARGS__); }), true))
#define LOG_UNLESS(test, fmt, ...) ((test) || (({ LOG(fmt, ##__VA_ARGS__); }), false))

// same as LOG, but don't call log_add_{last, only} to avoid calling breakpoint()
#define LOG_NOBREAK_pre                         \
  do {                                          \
  log_add_context();
#define LOG_NOBREAK_first(s, fmt) log_add_first((intptr_t)(s fmt));
#define LOG_NOBREAK_middle(x) log_add((intptr_t)(x));
#define LOG_NOBREAK_last(x) log_add_last((intptr_t)(x));
#define LOG_NOBREAK_only(s, fmt) log_add_only((intptr_t)(s fmt));
#define LOG_NOBREAK_post                        \
  } while(0)
#define LOG_NOBREAK_args LOG_args
#define LOG_NOBREAK(fmt, ...) FORARG(LOG_NOBREAK, __FILE__ ":" STRINGIFY(__LINE__) ": " fmt, ##__VA_ARGS__)

#endif

TEST(log) {
  log_init();
  LOG("test %d + %d = %d", 1, 2, 3);
  LOG("WAZZUP %s", "d00d");
  LOG("[%.*s]", 3, "12345");
  log_print_all();
  return 0;
}

#if INTERFACE
typedef struct context_s context_t;
struct context_s {
  struct context_s *next;
  char const *fmt;
  intptr_t arg[0];
};

#define CONTEXT_pre                             \
  __attribute__((cleanup(log_cleanup_context))) \
  intptr_t __context[] = {                      \
    (intptr_t)__log_context,
#define CONTEXT_first(s, fmt) (intptr_t)(s fmt) + 1,
#define CONTEXT_middle(x) (intptr_t)(x),
#define CONTEXT_last(x) (intptr_t)(x)};
#define CONTEXT_only(s, fmt) (intptr_t)(s fmt) + 1};
#define CONTEXT_post                            \
  __log_context = (context_t *)__context;
#define CONTEXT_args ("\xff\xc0", "\xff\xc1", "\xff\xc2", "\xff\xc3", "\xff\xc4", "\xff\xc5", "\xff\xc6", "\xff\xc7", "\xff\xc8")
#define CONTEXT(fmt, ...) FORARG(CONTEXT, __FILE__ ":" STRINGIFY(__LINE__) ": " fmt, ##__VA_ARGS__)
#endif

#if INTERFACE
#define CONTEXT_LOG_pre                                                 \
  const char *__context __attribute__((cleanup(log_cleanup_context_log))); \
  do {
#define CONTEXT_LOG_first(s, fmt)               \
  __context = s fmt;                            \
  log_add_first((intptr_t)(__context + 1));
#define CONTEXT_LOG_middle(x)                   \
  log_add((intptr_t)(x));
#define CONTEXT_LOG_last(x)                     \
  if(log_add_last((intptr_t)(x))) breakpoint();
#define CONTEXT_LOG_only(s, fmt)                                \
  __context = s fmt;                                            \
  if(log_add_only((intptr_t)(__context + 1))) breakpoint();
#define CONTEXT_LOG_post                        \
  } while(0)
#define CONTEXT_LOG_args ("\xff\x40", "\xff\x41", "\xff\x42", "\xff\x43", "\xff\x44", "\xff\x45", "\xff\x46", "\xff\x47", "\xff\x48")
#define CONTEXT_LOG(fmt, ...) FORARG(CONTEXT_LOG, __FILE__ ":" STRINGIFY(__LINE__) ": " fmt, ##__VA_ARGS__)
#endif

void log_cleanup_context(void *p) {
  context_t *ctx = p;
  if(ctx->fmt[0] == '\xff') {
    // add end marker
    log_add((intptr_t)ctx->fmt);
  }
  __log_context = ctx->next;
}

void log_cleanup_context_log(const char **fmt) {
  log_add((intptr_t)*fmt);
}

void log_add_context() {
  context_t *p = __log_context;
  while(p &&
        p->fmt[0] != '\xff') {
    log_add((intptr_t)p->fmt);
    uint8_t len = p->fmt[0] & ~MASK;
    COUNTUP(i, len) {
      log_add(p->arg[i]);
    }
    p->fmt--;
    p = p->next;
  }
}

static
void __test_context_c(int x) {
  CONTEXT_LOG("C %d", x);
}

static
void __test_context_b(int x) {
  CONTEXT("B %d", x);
  LOG_WHEN(x == 0, "(b) zero x");
}

static
void __test_context_a(int x) {
  CONTEXT("A %d", x);
  __test_context_b(x - 1);
  LOG_WHEN(x > 0, "(a) nonzero x");
}

static
void __test_context_e(int x) {
  CONTEXT_LOG("E %d", x);
}

static
void __test_context_d(int x) {
  CONTEXT_LOG("D %d", x);
  __test_context_e(x);
  LOG("exiting d");
}

TEST(context) {
  log_init();
  __test_context_a(2);
  __test_context_a(1);
  __test_context_a(0);
  __test_context_c(3);
  __test_context_d(42);
  log_print_all();
  return 0;
}

#if INTERFACE
typedef char tag_t[4];
#define FORMAT_TAG "%.4s"
#endif

char to_tag_char(int x) {
  x &= 31;
  if(x < 24) {
    return 'a' + x;
  } else {
    return '2' + x - 24;
  }
}

int from_tag_char(char c) {
  if(c >= 'a') {
    if(c <= 'x') {
      return c - 'a';
    } else {
      return -1;
    }
  } else if(c >= '0') {
    return c - '2' + 24;
  } else {
    return -1;
  }
}

int spread_bits(int x) {
  int y = 0;
  COUNTUP(i, 4) {
    int t = 0;
    COUNTUP(j, 5) {
      t <<= 4;
      t |= x & 1;
      x >>= 1;
    }
    t <<= i;
    y |= t;
  }
  return y;
}

int gather_bits(int y) {
  int x = 0;
  COUNTDOWN(i, 4) {
    int t = y >> i;
    COUNTUP(j, 5) {
      x <<= 1;
      x |= t & 1;
      t >>= 4;
    }
  }
  return x;
}

TEST(spread_gather_bits) {
  int x = 0x9AC35;
  int spread = spread_bits(x);
  int gather = gather_bits(spread);
  return x == gather ? 0 : -1;
}

// modular multiplicative inverses
const unsigned int tag_factor = 510199;
const unsigned int tag_factor_inverse = 96455;
const unsigned int tag_mask = 0x7ffff;

void write_tag(tag_t tag, unsigned int val) {
  val += 1;
  val *= tag_factor;
  val &= tag_mask;
  COUNTDOWN(i, sizeof(tag_t)) {
    tag[i] = to_tag_char(val);
    val >>= 5;
  }
}

int read_tag(const tag_t tag) {
  unsigned int val = 0;
  COUNTUP(i, sizeof(tag_t)) {
    int x = from_tag_char(tag[i]);
    if(x < 0) return x;
    val = (val << 5) | x;
  }
  val *= tag_factor_inverse;
  val &= tag_mask;
  return val - 1;
}

TEST(tag) {
  tag_t tag = "good";
  int x = read_tag(tag);
  write_tag(tag, x);
  printf("tag: %d = " FORMAT_TAG "\n", x, tag);
  return strncmp("good", tag, sizeof(tag)) == 0 ? 0 : -1;
}

void get_tag(tag_t tag) {
  write_tag(tag, msg_head);
}

#if INTERFACE
#define TWEAK(default_value, fmt, ...)                          \
  ({                                                            \
    const char *c;                                              \
    intptr_t x;                                                 \
    if unlikely(log_do_tweak(&x)) {                             \
      c = COLOR_blue;                                           \
    } else {                                                    \
      x = (default_value);                                      \
      c = COLOR_normal;                                         \
    }                                                           \
    LOG(COLORs("TWEAK(%d)") " " fmt, c, x, ##__VA_ARGS__);      \
    x;                                                          \
  })
#endif

bool log_do_tweak(intptr_t *x) {
  if unlikely(tweak_enabled && log_head == tweak_trigger) {
    *x = tweak_value;
    return true;
  } else {
    return false;
  }
}

void log_set_tweak(const tag_t tag, intptr_t value) {
  tweak_enabled = true;
  tweak_trigger = read_tag(tag);
  tweak_value = value;
}

void log_unset_tweak() {
  tweak_enabled = false;
}