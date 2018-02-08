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

#ifndef __RT_TYPES__
#define __RT_TYPES__

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <time.h>

#include "startle/macros.h"
#include "startle/types.h"
#include "macros.h"

typedef unsigned int uint;

typedef uint16_t csize_t;
typedef struct __attribute__((packed)) type {
  uint8_t exclusive, flags;
} type_t;

// exclusive types
#define T_ANY       0x00
#define T_INT       0x01
#define T_IO        0x02
#define T_LIST      0x03
#define T_SYMBOL    0x04
#define T_MAP       0x05
#define T_STRING    0x06
#define T_RETURN    0x07
#define T_FLOAT     0x08
#define T_FUNCTION  0x09
#define T_MODULE    0x0a
#define T_BOTTOM    0x7f

// type flags
#define T_DEP        0x02
#define T_CHANGES    0x04
#define T_INCOMPLETE 0x08
#define T_ROW        0x10
#define T_TRACED     0x20
#define T_FAIL       0x40
#define T_VAR        0x80


typedef struct type_request {
  csize_t in, out;
  uint8_t t, pos;
} type_request_t;

typedef struct cell cell_t;
typedef struct expr expr_t;
typedef struct value value_t;
typedef struct tok_list tok_list_t;
typedef struct entry entry_t;
typedef struct mem mem_t;

typedef uintptr_t alt_set_t;
typedef int16_t refcount_t;
typedef intptr_t val_t;

#ifdef __clang__
#pragma clang diagnostic ignored "-Warray-bounds"
#pragma clang diagnostic ignored "-Wzero-length-array"
#pragma clang diagnostic ignored "-Wgnu-anonymous-struct"
#pragma clang diagnostic ignored "-Wnested-anon-types"
#pragma clang diagnostic ignored "-Wgnu-folding-constant"
#pragma clang diagnostic ignored "-Wgnu-empty-initializer"
#pragma clang diagnostic ignored "-Wextended-offsetof"
#endif

typedef struct trace_cell {
  cell_t *entry;
  val_t index;
} trace_cell_t;

typedef bool (reduce_t)(cell_t **cell, type_request_t treq);

#define EXPR_NEEDS_ARG 0x01
#define EXPR_RECURSIVE 0x02
#define EXPR_TRACE     0x04
#define EXPR_NO_UNIFY  0x08

/* unevaluated expression */
struct __attribute__((packed)) expr {
  uint8_t out, flags;
  union {
    cell_t *arg[2];
    val_t idx[2];
    struct {
      cell_t *arg0;
      alt_set_t alt_set;
    };
  };
};

/* reduced value */
struct __attribute__((packed)) value {
  type_t type;
  alt_set_t alt_set;
  union {
    val_t integer[2]; /* integer */
    double flt[1];    /* float */
    cell_t *ptr[2];   /* list */
    pair_t map[1];    /* map */
    seg_t str;        /* string */
    trace_cell_t tc;  /* variable */
  };
};

/* token list */
struct __attribute__((packed)) tok_list {
  csize_t length;
  const char *location, *line;
  cell_t *next;
};

/* unallocated memory */
struct __attribute__((packed)) mem {
  csize_t __padding;
  cell_t *prev, *next;
};

#define ENTRY_PRIMITIVE 0x01
#define ENTRY_TRACE     0x02
#define ENTRY_RECURSIVE 0x04
#define ENTRY_QUOTE     0x08
#define ENTRY_ROW       0x10
#define ENTRY_MOV_VARS  0x20
#define ENTRY_COMPLETE  0x80

/* word entry */
struct __attribute__((packed)) entry {
  uint8_t rec, flags, alts, sub_id;
  csize_t in, out, len;
  cell_t *parent;
  cell_t *initial;
};

typedef enum char_class_t {
  CC_NONE,
  CC_NUMERIC,
  CC_FLOAT,
  CC_ALPHA,
  CC_SYMBOL,
  CC_BRACKET,
  CC_VAR,
  CC_COMMENT,
  CC_DOT
} char_class_t;


// define the op enum
#define OP__ITEM(name) \
  OP_##name,

typedef enum __attribute__((packed)) op {
  OP__ITEM(null)
  #include "op_list.h"
  OP_COUNT
} op;

#undef OP__ITEM

struct __attribute__((packed, aligned(4))) cell {
  /* op indicates the type:
   * OP_null      -> mem
   * OP_value     -> value
   * otherwise    -> expr
   */
  union {
    uintptr_t raw[8];
    struct {
      union {
        cell_t *alt;
        const char *word_name; // entry
      };
      union {
        cell_t *tmp;
        const char *module_name; // entry
        type_t expr_type; // trace
        char_class_t char_class; // tok_list
      };
      op op;
      uint8_t pos;
      refcount_t n;
      csize_t size;
      union {
        expr_t expr;
        value_t value;
        tok_list_t tok_list;
        entry_t entry;
        mem_t mem;
      };
    };
  };
};

static_assert(sizeof(cell_t) == sizeof_field(cell_t, raw), "cell_t wrong size");
static_assert(offsetof(cell_t, expr.arg[1]) == offsetof(cell_t, value.ptr[0]), "second arg not aliased with first ptr");

typedef struct stats_t {
  unsigned int reduce_cnt, fail_cnt, alloc_cnt, max_alloc_cnt;
  unsigned int current_alloc_cnt;
  clock_t start, stop;
  uint8_t alt_cnt;
} stats_t;

#ifdef EMSCRIPTEN
#define strnlen(s, n) strlen(s)
#endif

// Maximum number of alts
#define AS_SIZE (sizeof(alt_set_t) * 4)
#define AS_MASK ((alt_set_t)0x5555555555555555)
#define ALT_SET_IDS AS_SIZE

#define SYM_False 0
#define SYM_True  1
#define SYM_IO    2
#define SYM_Dict  3

#define PERSISTENT ((refcount_t)-15)

#define PRIMITIVE_MODULE_PREFIX __primitive
#define PRIMITIVE_MODULE_NAME STRINGIFY(PRIMITIVE_MODULE_PREFIX)

typedef struct list_iterator {
  cell_t **array;
  csize_t index, size;
  bool row;
} list_iterator_t;

void breakpoint();

#define COMMAND(name, desc) void command_##name(UNUSED cell_t *rest)

#endif
