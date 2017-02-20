/* Copyright 2012-2016 Dustin DeWeese
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

#include "rt_types.h"
#include <stdio.h>
#include <string.h>
#include <inttypes.h>
#include <assert.h>

#include "linenoise/linenoise.h"
#include "gen/cells.h"
#include "gen/rt.h"
#include "gen/test.h"
#include "gen/eval.h"
#include "gen/primitive.h"
#include "gen/special.h"
#include "gen/test_table.h"

pair_t tests[] = TESTS;

int test_alloc() {
  cell_t *a[30];
  LOOP(50) {
    FOREACH(i, a) {
      a[i] = func(func_add, 9, 1);
    }
    FOREACH(i, a) {
      closure_free(a[i]);
    }
  }
  return check_free() ? 0 : -1;
}

int test_loops() {
  COUNTUP(i, 3) {
    printf("up: %d\n", (unsigned int)i);
  }
  COUNTDOWN(i, 3) {
    printf("down: %d\n", (unsigned int)i);
  }
  COUNTUP(i, 0) {
    printf("down: shouldn't print this\n");
  }
  COUNTDOWN(i, 0) {
    printf("down: shouldn't print this\n");
  }

  unsigned int arr[] = {1, 4, 9};
  FOREACH(i, arr) {
    printf("arr[%d] = %d\n", (unsigned int)i, arr[i]);
  }
  LOOP(3) {
    LOOP(3) {
      putchar('x');
    }
    putchar('\n');
  }
  return 0;
}

bool check_free() {
  bool leak = false;
  FOREACH(i, cells) {
    cell_t *c = &cells[i];
    if(is_closure(c)) {
      if(c->n == PERSISTENT) {
        i += closure_cells(c) - 1;
      } else {
        printf("LEAK: %" PRIuPTR " (%u)\n", i, (unsigned int)cells[i].n);
        leak = true;
      }
    }
  }
  return !leak;
}

int run_test(const char *name) {
  int name_size = strlen(name);
  int fail = 0;
  FOREACH(i, tests) {
    pair_t *entry = &tests[i];
    char *entry_name = (char *)entry->first;
    int (*entry_func)() = (int (*)())entry->second;
    int entry_name_size = strlen(entry_name);
    if(strncmp(name, entry_name, min(name_size, entry_name_size)) == 0) {
      printf("@ %s\n", entry_name);
      int result = entry_func();
      printf("%s => %d\n", entry_name, result);
      if(result && !fail) fail = result;
    }
  }
  return fail;
}

#define TEST_2(x0, x1, x2, ...) printf("TEST2(" x0 ", " x1 ", " x2 ")\n")
#define TEST_1(...) printf("TEST1\n")

int test_macro_dispatch() {
  DISPATCH(TEST, 5, "1", "2", "3");
  DISPATCH(TEST, 5, "1", "2", "3", "4");
  return 0;
}

static
cell_t **flatten(cell_t *c, cell_t **tail) {
  c = clear_ptr(c);
  if(c && !c->tmp && tail != &c->tmp && c->n != PERSISTENT) {
    LIST_ADD(tmp, tail, c);
    traverse(c, {
        tail = flatten(*p, tail);
      }, PTRS | ALT | ARGS_IN);
  }
  return tail;
}

void print_list(cell_t *c) {
  if(c) {
    printf("{%d", (int)(c-cells));
    while((c = c->tmp)) {
      printf(", %d", (int)(c-cells));
    }
    printf("}\n");
  } else {
    printf("{}\n");
  }
}

static
void assert_ref_dec(cell_t *c) {
  while(c) {
    traverse(c, {
        cell_t *x = clear_ptr(*p);
        if(x && x->n != PERSISTENT) --x->n;
      }, PTRS | ALT | ARGS_IN);
    c = c->tmp;
  }
}

static
void assert_ref_inc(cell_t *c) {
  while(c) {
    traverse(c, {
        cell_t *x = clear_ptr(*p);
        if(x && x->n != PERSISTENT) ++x->n;
      }, PTRS | ALT | ARGS_IN);
    c = c->tmp;
  }
}

size_t count_root(const cell_t *c, cell_t ***roots, size_t n) {
  size_t cnt = 0;
  COUNTUP(i, n) {
    if(roots[i] && c == *roots[i]) cnt++;
  }
  return cnt;
}

static
void print_roots(cell_t ***roots, size_t n) {
  COUNTUP(i, n) {
    if(roots[i]) printf("root: %d\n", (int)((*roots[i])-cells));
  }
}

static
bool assert_ref_check(cell_t *c, cell_t ***roots, size_t roots_n) {
  bool res = true;
  while(c) {
    refcount_t n = c->n + 1;
    n -= count_root(c, roots, roots_n);
    if(n) {
      printf("assert_ref: cell[%d].n == %d\n", (int)(c - cells), (int)n);
      res = false;
    }
    c = c->tmp;
  }
  return res;
}

// check ref counts starting at root
bool assert_ref(cell_t ***roots, size_t n) {
  cell_t *list = 0, **tail = &list;
  COUNTUP(i, n) {
    if(!roots[i]) continue;
    tail = flatten(*roots[i], tail);
  }
  assert_ref_dec(list);
  bool check = assert_ref_check(list, roots, n);
  assert_ref_inc(list);
  clean_tmp(list);
  if(!check) print_roots(roots, n);
  return check;
}
