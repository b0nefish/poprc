/* Copyright 2012-2019 Dustin DeWeese
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
#include <stdio.h>
#include <inttypes.h>

#include "startle/error.h"
#include "startle/log.h"
#include "startle/test.h"
#include "startle/support.h"
#include "startle/map.h"

#include "cells.h"
#include "rt.h"
#include "primitive.h"
#include "special.h"
#include "byte_compile.h"
#include "parse.h"
#include "print.h"
#include "cgen.h"
#include "vlgen.h"
#include "eval.h"
#include "lex.h"
#include "user_func.h"
#include "trace.h"
#include "list.h"

#define MAX_DEGREE 31

// table of corresponding Verilog types
static
const char *vltype(type_t t) {
  static const char *table[] = {
    [T_ANY]      = "any",
    [T_INT]      = "int",
    [T_LIST]     = "stream",
    [T_SYMBOL]   = "sym",
    [T_MAP]      = "map",
    [T_STRING]   = "string",
    [T_FLOAT]    = "real",
    [T_OPAQUE]   = "opaque",
    [T_BOTTOM]   = "void",
  };
  assert_error(t < LENGTH(table));
  return table[t];
}

static
void gen_module_interface(const cell_t *e, const type_t *rtypes) {
  const cell_t *p = e + 1;
  csize_t out_n = e->entry.out;

  printf("module %s_%s (\n", e->module_name, e->word_name);
  char *sep = "";
  if(FLAG(*e, entry, SYNC)) {
    printf("  `sync_ports");
    sep = ",\n";
  }
  COUNTDOWN(i, e->entry.in) {
    const cell_t *a = &p[i];
    printf("%s  `input(%s, %d)", sep, vltype(trace_type(a)), (int)(e->entry.in - 1 - i));
    sep = ",\n";
  }

  COUNTUP(i, out_n) {
    printf("%s  `output(%s, %d)", sep, vltype(rtypes[i]), (int)i);
  }

  printf("\n);\n");
}

static
void gen_value_rhs(const cell_t *c) {
  type_t t = trace_type(c);
  switch(t) {
  case T_INT:
  case T_SYMBOL:
    printf("%d", (int)c->value.integer);
    break;
  case T_STRING: {
    seg_t str = value_seg(c);
    printf("\"");
    print_escaped_string(str);
    printf("\"");
    break;
  }
  case T_LIST: {
    assert_eq(list_size(c), 0);
    printf("`nil");
    break;
  }
  default:
    assert_error(false); // TODO add more types
  }
}

static bool is_sync(const cell_t *c) {
  if(ONEOF(c->op, OP_ap, OP_pushr, OP_compose)) return true; // ***
  cell_t *entry = get_entry(c);
  return entry && FLAG(*entry, entry, SYNC);
}

static
void gen_decls(cell_t *e) {
  FOR_TRACE_CONST(c, e) {
    int i = c - e;
    type_t t = trace_type(c);
    if(ONEOF(t, T_BOTTOM, T_RETURN)) continue;
    if(is_var(c)) {
      const char *decl = FLAG(*e, entry, RECURSIVE) ? "variable" : "alias";
      printf("  `%s(%s, %s%d, in%d);\n", decl, vltype(t), cname(t), i, e->entry.in - i);
    } else {
      if(c->op == OP_value) {
        printf("  `const(%s, %s%d, ", vltype(t), cname(t), i);
        gen_value_rhs(c);
        printf(");\n");
      } else if(!is_tail_call(e, c)) {
        if(is_sync(c)) {
          printf("  wire inst%d_in_ready;\n", i);
        }
        if(is_self_call(e, c)) {
          printf("  `reg(%s, %s%d) = 0;\n", vltype(t), cname(t), i); // ***
        } else {
          printf("  `wire(%s, %s%d);\n", vltype(t), cname(t), i);
        }
      } else if(t == T_LIST) {
        printf("  `const(%s, %s%d, `nil);\n", vltype(t), cname(t), i);
      }
    }
  }
  if(FLAG(*e, entry, STACK)) { // ***
    printf("\n"
           "  // stack\n"
           "  reg `intT stack[0:255];\n"
           "  reg [0:7] sp = 0;\n"
           "  reg reduce_stack = `false;\n");
  }
}

static
void find_sync_inputs(const cell_t *e, const cell_t *c, uintptr_t *set, size_t size, int depth) {
  while(ONEOF(c->op, OP_assert, OP_seq, OP_unless)) {
    if(c->op == OP_assert && !depth) set_insert(c-e, set, size);
    c = &e[tr_index(c->expr.arg[0])];
  }
  if(!depth && is_sync(c) && !is_tail_call(e, c)) {
    set_insert(c-e, set, size);
  } else if(is_var(c)) {
    if(!depth) set_insert(e->entry.len+1, set, size);
  } else {
    TRAVERSE(c, const, in, ptrs) {
      find_sync_inputs(e, &e[tr_index(*p)], set, size, max(0, depth-1));
    }
  }
}

static
void gen_sync_disjoint_inputs(const cell_t *e, const cell_t *c) {
  uintptr_t set[MAX_DEGREE] = {0};
  uintptr_t input_set[MAX_DEGREE] = {0};
  LOG("gen_sync_disjoint_inputs %E %d", e, c-e);
  find_sync_inputs(e, c, set, LENGTH(set), 1);
  FOREACH(i, set) {
    if(INRANGE(set[i], 1, e->entry.len)) {
      find_sync_inputs(e, &e[set[i]], input_set, LENGTH(input_set), 1);
    }
  }
  FOREACH(i, input_set) {
    if(input_set[i]) {
      set_remove(input_set[i], set, LENGTH(set));
    }
  }
  size_t inputs = squeeze(set, LENGTH(set));
  if(inputs) {
    COUNTUP(i, inputs) {
      if(i) printf(" & ");
      if(set[i] <= e->entry.len) {
        printf("inst%d_out_valid", (int)set[i]);
      } else {
        if(FLAG(*e, entry, RECURSIVE)) {
          printf("active");
        } else {
          printf("in_valid");
        }
      }
    }
  } else {
    printf("`true");
  }
}

static
void gen_sync_block(const cell_t *e, int block) {
  const char *sep = "";
  FOR_TRACE_CONST(c, e, block) {
    if(is_return(c)) break;
    if(is_user_func(c)) {
      cell_t *ce = get_entry(c);
      if(ce == e) break;
    }
    if(c->op == OP_assert || is_sync(c)) {
      printf("%sinst%d_out_valid", sep, (int)(c-e));
      sep = " & ";
    }
  }
  if(!sep[0]) printf("`true");
}

static
void find_sync_outputs(const cell_t *e, const cell_t *c, uintptr_t *set, size_t size, uintptr_t const *const *backrefs) {
  if(is_return(c)) return;
  const uintptr_t *outs;
  size_t n = get_outputs(e, c, backrefs, &outs);
  COUNTUP(i, n) {
    int oi = outs[i];
    const cell_t *out = &e[oi];
    if(is_tail_call(e, out)) {
      // stop
    } else if(is_return(out) || (is_sync(out))) {
      set_insert(oi, set, size);
    } else if(ONEOF(out->op, OP_unless, OP_assert, OP_seq) && cgen_lookup(e, out) != c - e) {
      // stop
    } else {
      find_sync_outputs(e, out, set, size, backrefs);
    }
  }
}

static
void gen_sync_disjoint_outputs(const cell_t *e, const cell_t *c, uintptr_t const *const *backrefs) {
  uintptr_t set[MAX_DEGREE] = {0};
  uintptr_t output_set[MAX_DEGREE] = {0};
  LOG("gen_sync_disjoint_outputs %E %d", e, c-e);
  if(c > e) {
    find_sync_outputs(e, c, set, LENGTH(set), backrefs);
  } else {
    COUNTUP(i, e->entry.in) {
      find_sync_outputs(e, &e[i+1], set, LENGTH(set), backrefs);
    }
  }
  FOREACH(i, set) {
    if(INRANGE(set[i], 1, e->entry.len)) {
      find_sync_outputs(e, &e[set[i]], output_set, LENGTH(output_set), backrefs);
    }
  }
  FOREACH(i, output_set) {
    if(output_set[i]) {
      set_remove(output_set[i], set, LENGTH(set));
    }
  }
  size_t outputs = squeeze(set, LENGTH(set));
  if(outputs) {
    bool top = false;
    COUNTUP(i, outputs) {
      if(i) printf(" & ");
      if(!is_return(&e[set[i]])) {
        printf("inst%d_in_ready", (int)set[i]);
      } else if(!top) {
        printf("out_ready");
        top = true;
      }
    }
  } else {
    printf("`true");
  }
}

static
void gen_instance(const cell_t *e, const cell_t *c, int *sync_chain, uintptr_t const *const *backrefs) {
  int inst = c - e;
  char *sep = "";
  const char *module_name, *word_name;
  bool sync = is_sync(c);
  csize_t
    in = closure_in(c),
    n = closure_args(c),
    start_out = n - closure_out(c);

  trace_get_name(c, &module_name, &word_name);
  type_t t = trace_type(c);

  if(sync) {
    printf("    `inst_sync(");
    print_function_name(e, c);
    printf(", inst%d)(\n      `sync(", inst);
    gen_sync_disjoint_inputs(e, c);
    printf(", ");
    gen_sync_disjoint_outputs(e, c, backrefs);
    printf("),");
  } else {
    printf("    `inst(");
    print_function_name(e, c);
    printf(", inst%d)(", inst);
  }

  COUNTUP(i, in) {
    int a = cgen_index(e, c->expr.arg[i]);
    type_t at = trace_type(&e[a]);
    printf("%s\n      `in(%s, %d, %s%d)",
           sep, vltype(at), (int)i, cname(at), a);
    sep = ",";
  };
  printf("%s\n      `out(%s, 0, %s%d)",
         sep, vltype(t), cname(t), inst);
  RANGEUP(i, start_out, n) {
    int a = cgen_index(e, c->expr.arg[i]);
    if(a > 0) {
      type_t at = trace_type(&e[a]);
      printf(",\n      `out(%s, %d, %s%d)",
             vltype(at), (int)(i - start_out + 1), cname(at), a);
    }
  }

  printf("\n    );\n");
  if(sync) *sync_chain = inst;
}

static
void gen_body(cell_t *e) {
  size_t backrefs_n = backrefs_size(e);
  assert_le(backrefs_n, 1024);
  uintptr_t const *backrefs[backrefs_n];
  build_backrefs(e, (uintptr_t **)backrefs, backrefs_n);
  bool block_start = true;
  int block = 1;
  int block_sync_chain = 0;
  if(FLAG(*e, entry, RECURSIVE)) {
    printf("\n  `loop_sync(");
    gen_sync_disjoint_outputs(e, e, backrefs);
    printf(");\n");
  } else if(FLAG(*e, entry, SYNC)) {
    printf("\n  `top_sync(");
    gen_sync_disjoint_outputs(e, e, backrefs);
    printf(");\n");
  }
  FOR_TRACE_CONST(c, e) {
    if(!is_value(c)) {
      if(!ONEOF(c->op, OP_dep, OP_seq, OP_unless)) {
        if(get_entry(c) == e) {
          if(FLAG(*c, trace, JUMP)) {
            TRAVERSE(c, const, in) {
              int i = tr_index(*p);
              const cell_t *a = &e[i];
              if(trace_type(a) == T_LIST && direct_refs(a) <= 1) { // ***
                printf("    assign %s%d_valid = `true;\n", cname(T_LIST), i);
              }
            }
          } else {
            goto next_block;
          }
          continue;
        }
        if(block_start) {
          printf("\n  `start_block(block%d)\n", block);
          block_start = false;
          block_sync_chain = 0;
        }
        if(c->op == OP_assert) {
          int ai = cgen_index(e, c->expr.arg[1]);
          printf("    `assert(inst%d, sym%d);\n", (int)(c - e), ai);
        } else {
          gen_instance(e, c, &block_sync_chain, backrefs);
        }
      }
    } else if(is_return(c)) {
    next_block:
      if(block_start) {
        printf("\n  `start_block(block%d)\n", block);
      }
      printf("    wire block%d_valid = ", block);
      gen_sync_block(e, block);
      printf(";\n");
      printf("  `end_block(block%d)\n", block);
      block = (c - e) + calculate_cells(c->size);
      block_start = true;
    }
  }
}

// TODO clean up
static
bool gen_outputs(const cell_t *e, const cell_t *r0, const type_t *rtypes) {
  bool ret = false;
  csize_t out = e->entry.out;
  COUNTUP(i, out) {
    type_t t = rtypes[i];
    printf("  assign out%d = ", (int)i);
    const cell_t *r = r0;
    const cell_t *r_prev = NULL;
    int block = 1;
    do {
      if(t == T_LIST || !is_tail_call(e, r)) {
        if(r_prev) {
          printf("\n      block%d_valid ? %s%d : ",
                 block, cname(t), cgen_index(e, r_prev->value.ptr[REVI(i)]));
          block = (r - e) + calculate_cells(r->size);
        }
        r_prev = r;
      }
      r = &e[tr_index(r->alt)];
    } while(r > e);
    assert_error(r_prev);
    printf("%s%d;\n", cname(t), cgen_index(e, r_prev->value.ptr[REVI(i)]));
    if(t == T_LIST) {
      printf("  assign out%d_valid = ", (int)i);
      r = r0;
      const char *sep = "";
      while(r > e) {
        printf("%s%s%d_valid", sep, cname(t), cgen_index(e, r_prev->value.ptr[REVI(i)]));
        r = &e[tr_index(r->alt)];
        sep = " | ";
      }
      printf(";\n");
      r = r0;
      while(r > e) {
        printf("  assign %s%d_ready = out%d_ready;\n", cname(t), cgen_index(e, r_prev->value.ptr[REVI(i)]), (int)i);
        r = &e[tr_index(r->alt)];
      }
    }
    ret = true;
  }
  return ret;
}

bool is_self_call(const cell_t *e, const cell_t *c) {
  return is_user_func(c) && get_entry(c) == e;
}

static
void gen_valid_ready(const cell_t *e, UNUSED const cell_t *r0) {
  const char *sep;
  int block;
  if(FLAG(*e, entry, RECURSIVE)) {
    printf("  wire not_valid = ");
    sep = "";
    block = 1;
    FOR_TRACE_CONST(c, e) {
      bool self_call = is_self_call(e, c);
      if(self_call) {
        printf("%sblock%d_valid", sep, block);
        sep = " | ";
      }
      if(is_return(c) ||
         (self_call && NOT_FLAG(*c, trace, JUMP))) {
        block = (c - e) + calculate_cells(c->size);
      }
    }
    if(!sep[0]) printf("`false");
    printf(";\n");
  }

  // valid
  printf("  wire valid%s = %s(",
         FLAG(*e, entry, STACK) ? "_ret" : "",
         FLAG(*e, entry, RECURSIVE) ? "!not_valid & " : "");
  sep = "";
  block = 1;
  FOR_TRACE_CONST(c, e) {
    if(block &&
       is_return(c) &&
       !is_tail_call(e, c)) {
      printf("%sblock%d_valid", sep, block);
      sep = " | ";
    }
    if(is_return(c)) {
      block = (c - e) + calculate_cells(c->size);
    } else if((is_self_call(e, c) &&
               NOT_FLAG(*c, trace, JUMP))) {
      block = 0;
    }
  }
  if(!sep[0]) printf("`true");
  printf(");\n");
  if(FLAG(*e, entry, STACK)) {
    printf("  wire valid = reduce_stack & ~|sp;\n");
  }
  printf("  assign out_valid = %svalid;\n", FLAG(*e, entry, RECURSIVE) ? "active & " : "");
}

static
void gen_loop(const cell_t *e, int block, const cell_t *tail_call) {
  csize_t in = e->entry.in;
  printf("      if(block%d_valid) begin\n", block);
  COUNTUP(i, in) {
    int a = cgen_index(e, tail_call->expr.arg[i]);
    type_t t = trace_type(&e[a]);
    assert_error(trace_type(&e[in - i]) == t);
    if(t != T_LIST) {
      if(NOT_FLAG(*tail_call, trace, JUMP)) { // ***
        printf("        stack[sp");
        if(i) printf(" + %d", (int)i);
        printf("] <= %s%d;\n", cname(t), (int)(in - i));
      }
      printf("        %s%d <= %s%d;\n",
             cname(t), (int)(in - i),
             cname(t), a);
    }
  }
  if(NOT_FLAG(*tail_call, trace, JUMP)) {
    printf("        sp <= sp + %d;\n", e->entry.in);
  }
  printf("      end\n");
}

static
void gen_loops(cell_t *e) {
  printf("  always @(posedge clk) begin\n"
         "    if(in_valid & ~active) begin\n");
  FOR_TRACE_CONST(c, e) {
    if(!is_var(c)) break;
    int i = c - e;
    type_t t = trace_type(c);
    if(t != T_LIST) printf("      %s%d <= in%d;\n", cname(t), i, e->entry.in - i);
  }
  printf("      `set(active);\n");
  if(FLAG(*e, entry, STACK))
    printf("      `reset(reduce_stack);\n");
  printf("    end\n"
         "    else if(valid) begin\n"
         "       if(out_ready) `reset(active);\n"
         "    end\n");
  if(FLAG(*e, entry, STACK)) {
    printf("    else if(valid_ret || reduce_stack) begin\n"
           "      `set(reduce_stack);\n");
    FOR_TRACE_CONST(c, e) {
      if(is_self_call(e, c) &&
         NOT_FLAG(*c, trace, JUMP)) {
        printf("      %s%d <= out0;\n",
               cname(trace_type(c)), (int)(c - e));
        cell_t *const *out = &c->expr.arg[closure_args(c) - closure_out(c)];
        COUNTUP(i, e->entry.out-1) {
          int x = tr_index(out[i]);
          printf("      %s%d <= out%d;\n",
                 cname(trace_type(&e[x])), x, (int)(i + 1));
        }
      }
    }
    RANGEUP(i, 1, e->entry.in + 1) {
      cell_t *v = &e[i];
      printf("      %s%d <= stack[sp - %d];\n",
             cname(trace_type(v)), (int)i, (int)REVI(i));
    }
    printf("      sp <= sp - %d;\n"
           "    end\n",
           e->entry.out); // ***
  }
  printf("    else begin\n");
  int start = 1;
  const cell_t *tail_call = NULL;
  FOR_TRACE_CONST(c, e) {
    if(is_return(c)) {
      if(tail_call) gen_loop(e, start, tail_call);
      start = (c - e) + calculate_cells(c->size);
      tail_call = NULL;
    } else if(get_entry(c) == e) {
      tail_call = c;
    }
  }
  printf("    end\n"
         "  end\n");
}

static
void gen_module(cell_t *e) {
  e->op = OP_value;
  const cell_t *r0 = NULL;
  FOR_TRACE_CONST(c, e) {
    if(is_return(c)) {
      r0 = c;
      break;
    }
  }
  type_t rtypes[e->entry.out];
  resolve_types(e, rtypes);
  gen_module_interface(e, rtypes);
  printf("\n");
  gen_decls(e);
  gen_body(e);
  printf("\n");
  if(FLAG(*e, entry, SYNC)) {
    gen_valid_ready(e, r0);
    printf("\n");
  }
  if(FLAG(*e, entry, RECURSIVE)) {
    gen_loops(e);
    printf("\n");
  }
  if(gen_outputs(e, r0, rtypes)) printf("\n");
  printf("endmodule\n");

  FOR_TRACE(c, e) {
    if(c->op == OP_exec && c->trace.type != T_BOTTOM) {
      cell_t *x = get_entry(c);
      if(x != e && x->op == OP_null) {
        printf("\n");
        gen_module(x);
      }
    }
  }
}

COMMAND(cv, "print Verilog code for given function") {
  if(rest) {
    command_define(rest);
    cell_t
      *m = eval_module(),
      *e = module_lookup_compiled(tok_seg(rest), &m);

    if(e) {
      gen_module(e);
      clear_ops(e);
    }
  }
  if(command_line) quit = true;
}
