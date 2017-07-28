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

#include "rt_types.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#include "gen/error.h"
#include "gen/cells.h"
#include "gen/rt.h"
#include "gen/primitive.h"
#include "gen/special.h"
#include "gen/test.h"
#include "gen/support.h"
#include "gen/byte_compile.h"
#include "gen/trace.h"
#include "gen/list.h"
#include "gen/user_func.h"
#include "gen/print.h"
#include "gen/log.h"

bool is_user_func(const cell_t *c) {
  return !is_value(c) && !!(c->expr.flags & FLAGS_USER_FUNC);
}

static
cell_t *map_cell(cell_t *code, intptr_t x) {
  return
    x == NIL_INDEX ? &nil_cell :
    x < 0 ? NULL :
    code[x].alt;
}

static
cell_t *get_return_arg(cell_t *code, cell_t *returns, intptr_t x) {
  trace_index_t i = trace_decode(returns->value.ptr[x]);
  return // can't use map_cell, returns empty_list() instead of &nil_cell
    i == NIL_INDEX ? empty_list() :
    i < 0 ? NULL :
    code[i].alt;
}

// given a list l with arity in -> out, produce an application of the list
// [X -> Y] => [X -> Y] apXY
cell_t *apply_list(cell_t *l, csize_t in, csize_t out) {
  cell_t *c = func(func_ap, in + 1, out + 1);
  if(in) {
    c->expr.arg[0] = (cell_t *)(intptr_t)(in - 1);
  } else {
    FLAG_CLEAR(c->expr.flags, FLAGS_NEEDS_ARG);
  }
  c->expr.arg[in] = l;
  RANGEUP(i, in+1, in+1+out) {
    c->expr.arg[i] = dep(c);
  }
  refn(c, out);
  return c;
}

// print a representation of a pattern for debugging
void print_pattern(cell_t *pattern) {
  if(is_var(pattern)) {
    printf(" ?%d", (int)(pattern->value.ptr[0]-trace_cur));
  } else if(is_list(pattern)) {
    csize_t in = function_in(pattern);
    cell_t **p;
    printf(" [");
    if(in) printf(" %d ->", in);
    FORLIST(p, pattern) {
      print_pattern(*p);
    }
    printf(" ]");
  } else {
    printf(" %c", 'A' + (char)((pattern-cells) % 26));
  }
}

// print the list of bindings for debugging
void print_bindings(cell_t *vl) {
  cell_t *entry = &trace_cur[-1];
  cell_t *base = entry + 1;
  FOLLOW(p, q, vl, tmp) {
    csize_t x = p->value.ptr[0] - base;
    printf("?%d = %d\n", x, (int)(q-cells));
  }
}

// print the word to match and its patterns
void print_word_pattern(cell_t *word) {
  printf("pattern:");
  COUNTUP(i, closure_in(word)) {
    print_pattern(word->expr.arg[i]);
  }
  printf("\n");
}

// build a zig-zag binding list by applying the pattern to c
// TODO add reduction back in
cell_t **bind_pattern(cell_t *c, cell_t *pattern, cell_t **tail) {
  assert_error(c);
  if(!pattern || !tail) return NULL;
  if(c == pattern) {
    // prune trivial matches
    return tail;
  } else if(is_var(pattern)) {
    // found a binding
    LIST_ADD(tmp, tail, pattern);
    LIST_ADD(tmp, tail, ref(c));
    return tail;
  } else if(is_list(pattern)) {
    csize_t
      in = function_in(pattern),
      out = function_out(pattern, false);
    if(out) {
      // this will rip the list apart (later)
      cell_t *l = apply_list(ref(c), in, out);
      drop(l); // don't care about the result

      list_iterator_t it = list_begin(pattern);
      COUNTDOWN(i, out) {
        cell_t **p = list_next(&it, false);
        assert_error(p);
        cell_t *d = l->expr.arg[in+1+i];
        tail = bind_pattern(d, *p, tail);
        drop(d);
      }
    }
    return tail;
  } else {
    return NULL;
  }
}

// unify c with pattern pat if possible, returning the unified result
cell_t *unify_convert(cell_t *c, cell_t *pat) {
  if(!pat) return NULL;
  csize_t e = closure_in(pat);
  cell_t *entry = &trace_cur[-1];
  if(c->size != pat->size ||
     c->expr.out != pat->expr.out ||
     c->expr.arg[e] != pat->expr.arg[e]) return NULL;
  csize_t
    in = entry->entry.in,
    out = entry->entry.out;
  cell_t *base = entry + 1;
  cell_t *ret = NULL;
  if(out != 1) { // for now
    LOG("unify_convert %d: out(%d) != 1", c-cells, out);
    return NULL;
  }

  cell_t *vl = 0;
  cell_t **tail = &vl;
  COUNTUP(i, closure_in(c)) {
    tail = bind_pattern(c->expr.arg[i], pat->expr.arg[i], tail);
    if(!tail) break;
  }
  //print_word_pattern(pat);
  if(tail) {
    //printf("pattern matched\n");
    cell_t *n = closure_alloc(in + out);
    n->func = func_exec;
    FLAG_SET(n->expr.flags, FLAGS_USER_FUNC);
    n->expr.arg[in] = entry;
    FLAG_SET(n->expr.flags, FLAGS_RECURSIVE);
    FOLLOW(p, q, vl, tmp) { // get arguments from the binding list
      csize_t x = p->value.ptr[0] - base;
      assert_error(x < in);
      n->expr.arg[in-1-x] = q;
      //printf("?%d = %d\n", x, (int)(q-cells));
    }
    COUNTUP(i, in) { // fill in missing arguments
      cell_t **a = &n->expr.arg[i];
      if(!*a) {
        cell_t *v = &base[in-1-i];
        *a = var_create(v->value.type.exclusive, v, 0, 0);
      }
    }
    ret = n;
  } else {
    FOLLOW(p, q, vl, tmp) {
      drop(q);
    }
  }
  clean_tmp(vl);
  return ret;
}

bool func_exec(cell_t **cp, type_request_t treq) {
  cell_t *c = *cp;
  assert_error(!is_marked(c));

  size_t in = closure_in(c);
  cell_t *entry = c->expr.arg[in];
  cell_t *code = entry + 1;
  size_t len = entry->entry.len;
  cell_t *res;
  cell_t *returns = NULL;
  type_t rtypes[entry->entry.out];
  CONTEXT("exec %s: %d 0x%x", entry->word_name, CELL_INDEX(c), c->expr.flags);

  // TODO remove this HACK
  int name_len = entry->word_name ? strlen(entry->word_name) : 0;
  bool underscore = name_len && entry->word_name[name_len - 1] == '_';

  // don't execute, just reduce all args and return variables
  if(len == 0 || // the function is being compiled
     (trace_enabled &&
      (c->expr.flags & FLAGS_RECURSIVE || // the function has already been expanded once
       (entry->entry.rec &&
        (initial_word ||
         underscore ||
         entry->entry.in > trace_cur[-1].entry.in))))) { // not the outermost function
    csize_t c_in = closure_in(c);
    alt_set_t alt_set = 0;
    unsigned int nonvar = 0;
    bool specialize = false;

    if(underscore) {
      LOG("underscore hack in %s.%s %d",
          entry->module_name,
          entry->word_name,
          CELL_INDEX(c));
    } else {
      // try to unify with initial_word, returning if successful
      cell_t *n = unify_convert(c, initial_word);
      if(n) {
        LOG("unified %s.%s %d with initial_word in %s.%s %d",
            entry->module_name,
            entry->word_name,
            CELL_INDEX(c),
            trace_cur[-1].module_name,
            trace_cur[-1].word_name,
            CELL_INDEX(initial_word));
        drop(c);
        *cp = n;
        return LOG_UNLESS(func_exec(cp, req_simple(T_BOTTOM)), "that didn't work");
      }
    }

    // reduce all inputs
    COUNTUP(i, c_in) {
      uint8_t t = len > 0 ? code[c_in - 1 - i].value.type.exclusive : T_ANY;
      if(t == T_FUNCTION) t = T_ANY; // HACK, T_FUNCTION breaks things
      if(!reduce_arg(c, i, &alt_set, req_simple(t)) ||
         as_conflict(alt_set)) goto fail;
      // if all vars in a recursive function, don't expand
      // TODO make this less dumb
      cell_t *a = clear_ptr(c->expr.arg[i]);
      if(!is_var(a)) nonvar++;
    }
    clear_flags(c);

    // HACK force lists on tail calls
    if(entry == &trace_cur[-1] || underscore) {
      COUNTUP(i, c_in) {
        if(is_list(c->expr.arg[i]) &&
           closure_is_ready(*leftmost(&c->expr.arg[i]))) {
          LOG("HACK forced cells[%d].expr.arg[%d]", CELL_INDEX(c), i);
          func_list(&c->expr.arg[i], req_simple(T_RETURN));

          // ensure quotes are stored first
          cell_t *l = c->expr.arg[i];
          c->expr.arg[i] = trace_quote_var(l);
          drop(l);
        }
      }
    }

    if(!underscore &&
       nonvar > 0 &&
       len > 0 &&
       !(c->expr.flags & FLAGS_RECURSIVE) &&
       (!entry->entry.rec || entry->entry.rec <= trace_cur[-1].entry.in))
    {
      // okay to expand
      goto expand;
    }

    cell_t *res;
    if(treq.t == T_BOTTOM) {
      COUNTUP(i, entry->entry.out) {
        rtypes[i].exclusive = T_BOTTOM;
      }
    } else if(entry->entry.len == 0) {
      COUNTUP(i, entry->entry.out) {
        rtypes[i].exclusive = T_ANY;
      }
    } else {
      resolve_types(entry, rtypes);
    }
    {
      uint8_t t = rtypes[0].exclusive;
      if(t == T_ANY) {
        t = c->expr.flags & FLAGS_RECURSIVE ? T_BOTTOM : treq.t;
      }
      if(t == T_FUNCTION) t = T_LIST;
      if(specialize && !dont_specialize) {
        res = trace_var_specialized(t, c);
      } else {
        res = var(t, c);
      }
    }
    res->value.alt_set = alt_set;
    res->alt = c->alt;

    // replace outputs with variables
    RANGEUP(i, c_in + 1, c_in + entry->entry.out) {
      cell_t *d = c->expr.arg[i];
      if(d && is_dep(d)) {
        assert_error(d->expr.arg[0] == c);
        drop(c);
        uint8_t t = rtypes[i].exclusive;
        if(t == T_FUNCTION) t = T_LIST;
        store_dep(d, res->value.ptr[0], i, t);
      }
    }

    store_reduced(cp, res);
    return true;

  fail:
    fail(cp, treq);
    return false;
  }

expand:
  assert_error(len);

  c->expr.arg[in] = 0;
  trace_clear_alt(entry); // *** probably shouldn't need this

  COUNTUP(i, in) {
    cell_t *p = &code[i];
    assert_error(is_var(p));
    p->alt = refn(c->expr.arg[in - 1 - i], p->n);
  }

  // allocate, copy, and index
  size_t s = 0;
  for(size_t i = in; i < len; i += s) { // TODO: rewrite with FORTRACE
    cell_t *p = &code[i];
    s = calculate_cells(p->size);
    if(!p->func) {
      p->alt = 0;
      continue; // skip empty cells TODO remove these
    }
    if(trace_type(p).exclusive == T_RETURN) {
      if(!returns) returns = p;
      continue;
    }
    cell_t *e = is_user_func(p) ? get_entry(p) : NULL;
    cell_t *nc;
    csize_t in;
    if(e && e->entry.out == 1 &&
       (in = closure_in(p)) < e->entry.in) {
      // wrap incomplete function in a quote
      LOG("quote wrap %d", (int)i);
      nc = closure_alloc(e->entry.in + 1);
      memcpy(nc, p, offsetof(cell_t, expr.arg));
      nc->size = e->entry.in + 1;
      csize_t remaining = e->entry.in - in;
      memcpy(&nc->expr.arg[remaining], p->expr.arg, in * sizeof(cell_t *));
      nc->expr.idx[0] = remaining - 1;
      nc->expr.arg[e->entry.in] = p->expr.arg[in];
      closure_set_ready(nc, false);
      nc = row_quote(nc);
    } else {
      nc = closure_alloc_cells(s);
      memcpy(nc, p, s * sizeof(cell_t));
    }
    nc->tmp = 0;
    p->alt = nc;
  }

  // check that a return was found
  assert_error(returns);

  // rewrite pointers
  for(size_t i = in; i < len; i += s) { // TODO: rewrite with FORTRACE
    cell_t *p = &code[i];
    cell_t *t = map_cell(code, i);
    s = calculate_cells(p->size);
    if((is_value(p) && p->value.type.exclusive == T_RETURN) || !t) continue;

    if(is_row_list(t)) t = t->value.ptr[0]; // for row quotes created above

    // skip rewriting for the entry argument
    cell_t **t_entry = NULL;
    if(is_user_func(t)) {
      t_entry = &t->expr.arg[closure_in(t)];
      *t_entry = get_entry(t);
    }

    TRAVERSE(t, alt, args, ptrs) {
      if(p != t_entry && *p) {
        trace_index_t x = trace_decode(*p);
        *p = map_cell(code, x);
      }
    }

    if(trace_enabled &&
       t_entry &&
       *t_entry == entry &&
       !initial_word) { // mark recursion
      FLAG_SET(t->expr.flags, FLAGS_RECURSIVE);
      initial_word = copy(c);
      initial_word->expr.arg[in] = entry;
      TRAVERSE_REF(initial_word, alt, in);
    }
  }

  // handle returns
  uint8_t alt_n = int_log2(entry->entry.alts);
  uint8_t alt_id = new_alt_id(alt_n);
  unsigned int branch = 0;
  size_t
    out = closure_out(c),
    n = closure_args(c);
  cell_t **results[out + 1];
  results[out] = &res;
  COUNTUP(i, out) {
    results[i] = &c->expr.arg[n - 1 - i];
  }

  // first one
  alt_set_t alt_set = as_multi(alt_id, alt_n, branch++);
  res = id(get_return_arg(code, returns, out), alt_set);
  COUNTUP(i, out) {
    cell_t *d = c->expr.arg[n - 1 - i];
    store_lazy_dep(d, get_return_arg(code, returns, i), alt_set);
  }

  // rest
  trace_index_t next = trace_decode(returns->alt);
  while(next >= 0) {
    alt_set_t as = as_multi(alt_id, alt_n, branch++);
    returns = &code[next];
    FOREACH(i, results) {
      cell_t *a = get_return_arg(code, returns, i);
      results[i] = &(*results[i])->alt;
      *results[i] = a ? id(a, as) : NULL;
    }
    next = trace_decode(returns->alt);
  }

  store_lazy(cp, c, res, 0);
  return false;
}

void reduce_quote(cell_t **cp) {
  if(is_user_func(*cp) && closure_is_ready(*cp)) { // HACKy
    LOG("HACK reduce_quote[%d]", CELL_INDEX(*cp));
    insert_root(cp);
    reduce(cp, req_any);
    remove_root(cp);
  }
}
