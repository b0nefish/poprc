/* Copyright 2012-2018 Dustin DeWeese
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

#include <string.h>
#include <stdio.h>
#include <math.h>
#include "rt_types.h"

#include "startle/error.h"
#include "startle/support.h"
#include "startle/log.h"

#include "cells.h"
#include "rt.h"
#include "primitive.h"
#include "special.h"
#include "print.h"
#include "trace.h"
#include "list.h"
#include "builders.h"

   /*-----------------------------------------------,
    |          VARIABLE NAME CONVENTIONS            |
    |-----------------------------------------------|
    |                                               |
    |  cell_t *c = closure being reduced            |
    |  cell_t *d = first dep                        |
    |    (second closure returned, lower on stack)  |
    |  cell_t *e = second dep                       |
    |  cell_t *f = ...                              |
    |  cell_t *p = expr.arg[0], leftmost arg        |
    |  cell_t *q = expr.arg[1]                      |
    |  bool s = success                             |
    |  cell_t *res = result to be stored in c       |
    |  cell_t *res_X = result to be stored in X     |
    |                                               |
    '-----------------------------------------------*/

cell_t *_op2(cell_t *c, uint8_t t, val_t (*op)(val_t, val_t), cell_t *x, cell_t *y) {
  if(is_var(x) || is_var(y)) {
    return var(t, c);
  } else {
    return val(t, op(x->value.integer,
                     y->value.integer));
  }
}

cell_t *_op1(cell_t *c, uint8_t t, val_t (*op)(val_t), cell_t *x) {
  if(is_var(x)) {
    return var(t, c);
  } else {
    return val(t, op(x->value.integer));
  }
}

response func_op2(cell_t **cp, type_request_t treq, int arg_type, int res_type, val_t (*op)(val_t, val_t), bool nonzero) {
  cell_t *res = 0;
  PRE(op2);

  CHECK_IF(!check_type(treq.t, res_type), FAIL);

  alt_set_t alt_set = 0;
  type_request_t atr = REQ(t, arg_type);
  CHECK(reduce_arg(c, 0, &alt_set, atr));
  CHECK(reduce_arg(c, 1, &alt_set, atr));
  CHECK_IF(as_conflict(alt_set), FAIL);
  CHECK_DELAY();
  clear_flags(c);

  cell_t *p = c->expr.arg[0], *q = c->expr.arg[1];
  CHECK_IF(nonzero && !is_var(q) && q->value.integer == 0, FAIL); // TODO assert this for variables
  res = _op2(c, res_type, op, p, q);
  res->alt = c->alt;
  res->value.alt_set = alt_set;
  add_conditions(res, p, q);
  store_reduced(cp, res);
  return SUCCESS;

 abort:
  return abort_op(rsp, cp, treq);
}

response func_op1(cell_t **cp, type_request_t treq, int arg_type, int res_type, val_t (*op)(val_t), val_t (*inv_op)(val_t)) {
  cell_t *res = 0;
  PRE(op1);

  CHECK_IF(!check_type(treq.t, res_type), FAIL);

  alt_set_t alt_set = 0;
  type_request_t atr = REQ(t, arg_type, REQ_INV(inv_op));
  CHECK(reduce_arg(c, 0, &alt_set, atr));
  CHECK_DELAY();
  clear_flags(c);

  cell_t *p = c->expr.arg[0];
  res = _op1(c, res_type, op, p);
  res->alt = c->alt;
  res->value.alt_set = alt_set;
  add_conditions(res, p);
  store_reduced(cp, res);
  return SUCCESS;

 abort:
  return abort_op(rsp, cp, treq);
}

cell_t *_op2_float(double (*op)(double, double), cell_t *x, cell_t *y) {
  return float_val(op(x->value.flt,
                      y->value.flt));
}

cell_t *_op1_float(double (*op)(double), cell_t *x) {
  return float_val(op(x->value.flt));
}

response func_op2_float(cell_t **cp, type_request_t treq, double (*op)(double, double), bool nonzero) {
  cell_t *res = 0;
  PRE(op2_float);

  CHECK_IF(!check_type(treq.t, T_FLOAT), FAIL);

  alt_set_t alt_set = 0;
  type_request_t atr = REQ(float);
  CHECK(reduce_arg(c, 0, &alt_set, atr));
  CHECK(reduce_arg(c, 1, &alt_set, atr));
  CHECK_IF(as_conflict(alt_set), FAIL);
  CHECK_DELAY();
  clear_flags(c);

  cell_t *p = c->expr.arg[0], *q = c->expr.arg[1];
  CHECK_IF(nonzero && !is_var(q) && q->value.flt == 0.0, FAIL); // TODO assert this for variables
  res = is_var(p) || is_var(q) ? var(treq.t, c) : _op2_float(op, p, q);
  res->value.type = T_FLOAT;
  res->alt = c->alt;
  res->value.alt_set = alt_set;
  add_conditions(res, p, q);
  store_reduced(cp, res);
  return SUCCESS;

 abort:
  return abort_op(rsp, cp, treq);
}

response func_op1_float(cell_t **cp, type_request_t treq, double (*op)(double)) {
  cell_t *res = 0;
  PRE(op1_float);

  CHECK_IF(!check_type(treq.t, T_FLOAT), FAIL);

  alt_set_t alt_set = 0;
  type_request_t atr = REQ(float);
  CHECK(reduce_arg(c, 0, &alt_set, atr));
  CHECK_DELAY();
  clear_flags(c);

  cell_t *p = c->expr.arg[0];
  res = is_var(p) ? var(treq.t, c) : _op1_float(op, p);
  res->value.type = T_FLOAT;
  res->alt = c->alt;
  res->value.alt_set = alt_set;
  add_conditions(res, p);
  store_reduced(cp, res);
  return SUCCESS;

 abort:
  return abort_op(rsp, cp, treq);
}

WORD("+", add, 2, 1)
val_t add_op(val_t x, val_t y) { return x + y; }
OP(add) {
  return func_op2(cp, treq, T_INT, T_INT, add_op, false);
}

WORD("*", mul, 2, 1)
val_t mul_op(val_t x, val_t y) { return x * y; }
OP(mul) {
  return func_op2(cp, treq, T_INT, T_INT, mul_op, false);
}

WORD("-", sub, 2, 1)
val_t sub_op(val_t x, val_t y) { return x - y; }
OP(sub) {
  return func_op2(cp, treq, T_INT, T_INT, sub_op, false);
}

WORD("/", div, 2, 1)
val_t div_op(val_t x, val_t y) { return x / y; }
OP(div) {
  return func_op2(cp, treq, T_INT, T_INT, div_op, true);
}

WORD("%", mod, 2, 1)
val_t mod_op(val_t x, val_t y) { return x % y; }
OP(mod) {
  return func_op2(cp, treq, T_INT, T_INT, mod_op, true);
}

WORD("+f", add_float, 2, 1)
double add_float_op(double x, double y) { return x + y; }
OP(add_float) {
  return func_op2_float(cp, treq, add_float_op, false);
}

WORD("*f", mul_float, 2, 1)
double mul_float_op(double x, double y) { return x * y; }
OP(mul_float) {
  return func_op2_float(cp, treq, mul_float_op, false);
}

WORD("-f", sub_float, 2, 1)
double sub_float_op(double x, double y) { return x - y; }
OP(sub_float) {
  return func_op2_float(cp, treq, sub_float_op, false);
}

WORD("/f", div_float, 2, 1)
double div_float_op(double x, double y) { return x / y; }
OP(div_float) {
  return func_op2_float(cp, treq, div_float_op, true);
}

WORD("log", log, 1, 1)
OP(log) {
  return func_op1_float(cp, treq, log);
}

WORD("exp", exp, 1, 1)
OP(exp) {
  return func_op1_float(cp, treq, exp);
}

WORD("cos", cos, 1, 1)
OP(cos) {
  return func_op1_float(cp, treq, cos);
}

WORD("sin", sin, 1, 1)
OP(sin) {
  return func_op1_float(cp, treq, sin);
}

WORD("tan", tan, 1, 1)
OP(tan) {
  return func_op1_float(cp, treq, tan);
}

WORD("atan2", atan2, 2, 1)
OP(atan2) {
  return func_op2_float(cp, treq, atan2, false);
}

WORD("&b", bitand, 2, 1)
val_t bitand_op(val_t x, val_t y) { return x & y; }
OP(bitand) {
  return func_op2(cp, treq, T_INT, T_INT, bitand_op, false);
}

WORD("|b", bitor, 2, 1)
val_t bitor_op(val_t x, val_t y) { return x | y; }
OP(bitor) {
  return func_op2(cp, treq, T_INT, T_INT, bitor_op, false);
}

WORD("^b", bitxor, 2, 1)
val_t bitxor_op(val_t x, val_t y) { return x ^ y; }
OP(bitxor) {
  return func_op2(cp, treq, T_INT, T_INT, bitxor_op, false);
}

WORD("<<b", shiftl, 2, 1)
val_t shiftl_op(val_t x, val_t y) { return x << y; }
OP(shiftl) {
  return func_op2(cp, treq, T_INT, T_INT, shiftl_op, false);
}

WORD(">>b", shiftr, 2, 1)
val_t shiftr_op(val_t x, val_t y) { return x >> y; }
OP(shiftr) {
  return func_op2(cp, treq, T_INT, T_INT, shiftr_op, false);
}

WORD("~b", complement, 1, 1)
val_t complement_op(val_t x) { return ~x; }
OP(complement) {
  return func_op1(cp, treq, T_INT, T_INT, complement_op, complement_op);
}

WORD("not", not, 1, 1)
val_t not_op(val_t x) { return !x; }
OP(not) {
  return func_op1(cp, treq, T_SYMBOL, T_SYMBOL, not_op, not_op);
}

WORD(">", gt, 2, 1)
val_t gt_op(val_t x, val_t y) { return x > y; }
OP(gt) {
  return func_op2(cp, treq, T_INT, T_SYMBOL, gt_op, false);
}

WORD(">=", gte, 2, 1)
val_t gte_op(val_t x, val_t y) { return x >= y; }
OP(gte) {
  return func_op2(cp, treq, T_INT, T_SYMBOL, gte_op, false);
}

WORD("<", lt, 2, 1)
val_t lt_op(val_t x, val_t y) { return x < y; }
OP(lt) {
  return func_op2(cp, treq, T_INT, T_SYMBOL, lt_op, false);
}

WORD("<=", lte, 2, 1)
val_t lte_op(val_t x, val_t y) { return x <= y; }
OP(lte) {
  return func_op2(cp, treq, T_INT, T_SYMBOL, lte_op, false);
}

WORD("==", eq, 2, 1)
WORD("=:=", eq_s, 2, 1)
val_t eq_op(val_t x, val_t y) { return x == y; }
OP(eq) {
  return func_op2(cp, treq, T_INT, T_SYMBOL, eq_op, false);
}
OP(eq_s) {
  return func_op2(cp, treq, T_SYMBOL, T_SYMBOL, eq_op, false);
}

WORD("!=", neq, 2, 1)
WORD("!:=", neq_s, 2, 1)
val_t neq_op(val_t x, val_t y) { return x != y; }
OP(neq) {
  return func_op2(cp, treq, T_INT, T_SYMBOL, neq_op, false);
}
OP(neq_s) {
  return func_op2(cp, treq, T_SYMBOL, T_SYMBOL, neq_op, false);
}

WORD("->f", to_float, 1, 1)
OP(to_float) {
  cell_t *res = 0;
  PRE(to_float);

  CHECK_IF(!check_type(treq.t, T_FLOAT), FAIL);

  alt_set_t alt_set = 0;
  CHECK(reduce_arg(c, 0, &alt_set, REQ(int)));
  CHECK_DELAY();
  clear_flags(c);

  cell_t *p = c->expr.arg[0];
  if(is_var(p)) {
    res = var(T_FLOAT, c);
  } else {
    res = float_val(p->value.integer);
  }
  res->value.type = T_FLOAT;
  res->alt = c->alt;
  res->value.alt_set = alt_set;
  add_conditions(res, p);
  store_reduced(cp, res);
  return SUCCESS;

 abort:
  return abort_op(rsp, cp, treq);
}

WORD("trunc", trunc, 1, 1)
OP(trunc) {
  cell_t *res = 0;
  PRE(to_float);

  CHECK_IF(!check_type(treq.t, T_INT), FAIL);

  alt_set_t alt_set = 0;
  CHECK(reduce_arg(c, 0, &alt_set, REQ(float)));
  CHECK_DELAY();
  clear_flags(c);

  cell_t *p = c->expr.arg[0];
  if(is_var(p)) {
    res = var(T_INT, c);
  } else {
    res = int_val(p->value.flt);
  }
  res->value.type = T_INT;
  res->alt = c->alt;
  res->value.alt_set = alt_set;
  add_conditions(res, p);
  store_reduced(cp, res);
  return SUCCESS;

 abort:
  return abort_op(rsp, cp, treq);
}

WORD("pushr", pushr, 2, 1)
OP(pushr) {
  cell_t *c = *cp;

  // lower to compose
  c = expand(c, 1);
  c->expr.arg[2] = empty_list();
  c->op = OP_compose;
  *cp = c;
  return RETRY;
}

WORD("|", alt, 2, 1)
OP(alt) {
  PRE(alt);
  uint8_t a = new_alt_id(1);
  cell_t *r0 = id(c->expr.arg[0], as_single(a, 0));
  cell_t *r1 = id(c->expr.arg[1], as_single(a, 1));
  r0->alt = r1;
  store_lazy(cp, r0, 0);
  return RETRY;
}

WORD("!", assert, 2, 1)
OP(assert) {
  PRE(assert);

  cell_t *res = NULL;
  cell_t *tc = NULL;
  alt_set_t alt_set = 0;
  CHECK(reduce_arg(c, 1, &alt_set, REQ(symbol, SYM_True)));
  CHECK_DELAY();
  cell_t *p = clear_ptr(c->expr.arg[1]);
  bool p_var = is_var(p);

  CHECK_IF(!p_var &&
           p->value.integer != SYM_True, FAIL);

  if(p_var) {
    CHECK_IF(treq.delay_assert, DELAY);
    tc = trace_partial(OP_assert, 1, p);
  }

  CHECK(reduce_arg(c, 0, &alt_set, treq));
  CHECK_IF(as_conflict(alt_set), FAIL);
  CHECK_DELAY();
  clear_flags(c);
  cell_t **q = &c->expr.arg[0];

  if(p_var && is_var(*q)) {
    res = var_create((*q)->value.type, tc, 0, 0);
    trace_arg(tc, 0, *q);
  } else {
    // handle is_var(*q)?
    res = take(q);
    unique(&res);
    drop(res->alt);
    add_conditions_var(res, tc, p);
  }
  res->value.alt_set = alt_set;
  res->alt = c->alt;

  store_reduced(cp, res);
  return SUCCESS;

 abort:
  return abort_op(rsp, cp, treq);
}

// for internal use
// very similar to assert
WORD("seq", seq, 2, 1)
OP(seq) {
  PRE(seq);

  cell_t *res = NULL;
  cell_t *tc = NULL;
  alt_set_t alt_set = 0;
  CHECK(reduce_arg(c, 1, &alt_set, REQ(any))); // don't split arg here?
  CHECK_DELAY();
  cell_t *p = clear_ptr(c->expr.arg[1]);
  bool p_var = is_var(p);

  if(p_var) {
    CHECK_IF(treq.delay_assert, DELAY);
    tc = trace_partial(OP_seq, 1, p);
  }

  CHECK(reduce_arg(c, 0, &alt_set, treq));
  CHECK_IF(as_conflict(alt_set), FAIL);
  CHECK_DELAY();
  clear_flags(c);
  cell_t **q = &c->expr.arg[0];

  if(p_var && is_var(*q)) {
    res = var_create((*q)->value.type, tc, 0, 0);
    trace_arg(tc, 0, *q);
  } else {
    res = take(q);
    unique(&res);
    drop(res->alt);
    add_conditions_var(res, tc, p);
  }
  res->value.alt_set = alt_set;
  res->alt = c->alt;

  store_reduced(cp, res);
  return SUCCESS;

 abort:
  return abort_op(rsp, cp, treq);
}

// very similar to assert
// TODO merge common code
WORD("otherwise", otherwise, 2, 1)
OP(otherwise) {
  PRE(otherwise);

  cell_t *res = NULL;
  cell_t *tc = NULL;
  alt_set_t alt_set = 0;
  response rsp0 = reduce(&c->expr.arg[0], REQ(any));
  CHECK_IF(rsp0 == DELAY, DELAY);
  cell_t *p = clear_ptr(c->expr.arg[0]);
  bool p_var = is_var(p);

  CHECK_IF(!p_var &&
           !p->value.var &&
           rsp0 != FAIL, FAIL);

  if(rsp0 != FAIL) {
    CHECK_IF(treq.delay_assert, DELAY);
    tc = trace_partial(OP_otherwise, 0, p);
    // alt?
  }

  CHECK(reduce_arg(c, 1, &alt_set, treq));
  CHECK_DELAY();
  clear_flags(c);
  cell_t **q = &c->expr.arg[1];

  if(p_var && is_var(*q)) {
    res = var_create((*q)->value.type, tc, 0, 0);
    trace_arg(tc, 1, *q);
  } else {
    // handle is_var(*q)?
    res = take(q);
    unique(&res);
    drop(res->alt);
    add_conditions_var(res, tc);
  }
  res->value.alt_set = alt_set;
  res->alt = c->alt;

  store_reduced(cp, res);
  return SUCCESS;

abort:
  return abort_op(rsp, cp, treq);
}

WORD("id", id, 1, 1)
OP(id) {
  PRE(id);
  alt_set_t alt_set = c->expr.alt_set;
  int pos = c->pos;

  if(alt_set || c->alt) {
    CHECK(reduce_arg(c, 0, &alt_set, treq));
    CHECK_IF(as_conflict(alt_set), FAIL);
    CHECK_DELAY();
    clear_flags(c);
    cell_t *res = mod_alt(ref(c->expr.arg[0]), c->alt, alt_set);
    mark_pos(res, pos);
    store_reduced(cp, res);
    return SUCCESS;
  } else {
    *cp = CUT(c, expr.arg[0]);
    mark_pos(*cp, pos);
    return RETRY;
  }

 abort:
  return abort_op(rsp, cp, treq);
}

WORD("drop", drop, 2, 1)
OP(drop) {
  PRE(drop);
  *cp = CUT(c, expr.arg[0]);
  return RETRY;
}

WORD("swap", swap, 2, 2)
OP(swap) {
  PRE(swap);
  int pos = c->pos;
  store_lazy_dep(c->expr.arg[2],
                 c->expr.arg[0], 0);
  store_lazy(cp, c->expr.arg[1], 0);
  mark_pos(*cp, pos);
  return RETRY;
}

// for testing
WORD("delay", delay, 1, 1)
OP(delay) {
  PRE(delay);
  if(treq.priority < 1) {
    LOG("delay (priority %d) %C", treq.priority, c);
    return DELAY;
  }

  store_lazy(cp, c->expr.arg[0], 0);
  return RETRY;
}

cell_t *id(cell_t *c, alt_set_t as) {
  if(!c) return NULL;
  cell_t *i = build_id(c);
  i->expr.alt_set = as;
  return i;
}

WORD("dup", dup, 1, 2)
OP(dup) {
  PRE(dup);
  cell_t *d = c->expr.arg[1];
  store_lazy_dep(d, ref(c->expr.arg[0]), 0);
  store_lazy(cp, c->expr.arg[0], 0);
  return RETRY;
}

// outputs required from the left operand given the rignt operand
csize_t function_compose_out(cell_t *c, csize_t arg_in, csize_t out) {
  c = clear_ptr(c);
  return csub(function_in(c) + csub(out, function_out(c, true)), arg_in);
}

// inputs required from the right operand given the left operand
csize_t function_compose_in(cell_t *c, csize_t req_in, csize_t arg_in, bool row) {
  c = clear_ptr(c);
  return csub(req_in, function_in(c)) + function_out(c, row) + arg_in;
}

static
response func_compose_ap(cell_t **cp, type_request_t treq, bool row) {
  CONTEXT("%s: %C", row ? "compose" : "ap", *cp);
  PRE_NO_CONTEXT(compose_ap);

  const csize_t
    in = closure_in(c) - 1,
    arg_in = in - row,
    n = closure_args(c),
    out = closure_out(c);

  cell_t *p = NULL;
  cell_t *res = NULL;
  int pos = c->pos ? c->pos : c->expr.arg[in]->pos;

  alt_set_t alt_set = 0;
  if(row) {
    CHECK(reduce_arg(c, 0, &alt_set, REQ(list, treq.in, 0)));
    CHECK_DELAY();
    p = clear_ptr(c->expr.arg[0]);
  }
  CHECK(reduce_arg(c, in, &alt_set,
                   REQ(list,
                       function_compose_in(p, out ? 0 : treq.in, arg_in, false /*_1_*/),
                       treq.out + out)));
  CHECK_IF(as_conflict(alt_set), FAIL);
  CHECK_DELAY();
  clear_flags(c);
  if(row) {
    placeholder_extend(&c->expr.arg[0], treq.in, function_compose_out(c->expr.arg[in], arg_in, treq.out + out));
    p = clear_ptr(c->expr.arg[0]);
  }
  cell_t **q = &c->expr.arg[in];
  placeholder_extend(q, function_compose_in(p, treq.in, arg_in, true /*_1_*/), treq.out + out);
  // *** _1_ don't know if/why this works

  list_iterator_t it;
  reverse_ptrs((void **)c->expr.arg, in);
  it.array = c->expr.arg;
  it.index = 0;
  it.size = in - row;
  it.row = row;

  // Maybe remove these?
  // *** prevent leaking outside variables into lists
  if(row && !pos) pos = p->pos;
  if(!pos) pos = (*q)->pos;

  cell_t *l = compose(it, ref(*q)); // TODO prevent leaking outside variables
  reverse_ptrs((void **)c->expr.arg, in);

  insert_root(q);
  it = list_begin(l);

  list_iterator_t end = it;
  LOOP(out) list_next(&end, false);
  res = list_rest(end);
  unique(&res);
  drop(res->alt);
  res->alt = c->alt;
  res->value.alt_set = alt_set;
  //res->pos = pos; // ***
  add_conditions(res, p, *q);

  COUNTUP(i, out) {
    cell_t **x = list_next(&it, false);
    if(!x) {
      drop(l);
      LOG("null quote output");
      ABORT(FAIL);
    }
    cell_t *d = c->expr.arg[n-1-i];
    mark_pos(*x, pos);
    cell_t *seq_x = build_seq(ref(*x), ref(res));
    store_lazy_dep(d, seq_x, alt_set);
    LOG_WHEN(res->alt, MARK("WARN") " alt seq dep %C <- %C #condition", d, seq_x);
    if(d) d->pos = pos; // ***
  }
  remove_root(q);

  drop(l);
  store_reduced(cp, res);
  ASSERT_REF();
  return SUCCESS;

 abort:
  drop(res);
  return abort_op(rsp, cp, treq);
}

WORD_ALIAS("pushl", ap, 2, 1, pushl)
WORD_ALIAS("popr", ap, 1, 2, popr)
OP(ap) {
  return func_compose_ap(cp, treq, false);
}

WORD(".", compose, 2, 1)
OP(compose) {
  return func_compose_ap(cp, treq, true);
}

WORD("print", print, 2, 1)
OP(print) {
  cell_t *res = 0;
  PRE(print);

  CHECK_IF(!check_type(treq.t, T_SYMBOL), FAIL);

  alt_set_t alt_set = 0;
  CHECK(reduce_arg(c, 0, &alt_set, REQ(symbol)));
  CHECK(reduce_arg(c, 1, &alt_set, REQ(any)));
  CHECK_IF(as_conflict(alt_set), FAIL);
  CHECK_DELAY();
  clear_flags(c);

  if(c->alt) {
    drop(c->alt);
    c->alt = 0;
  }

  cell_t *p = c->expr.arg[0], *q = c->expr.arg[1];
  if(is_var(p) || is_var(q)) {
    res = var(T_SYMBOL, c);
  } else if(p->value.integer == SYM_IO) {
    show_one(q);
    res = ref(p);
  } else {
    ABORT(FAIL);
  }
  store_reduced(cp, res);
  return SUCCESS;

 abort:
  drop(res);
  return abort_op(rsp, cp, treq);
}

bool is_list_var(cell_t *c) {
  return is_row_list(c) && is_placeholder(c->value.ptr[0]);
}

response func_type(cell_t **cp, type_request_t treq, uint8_t type) {
  PRE(type);

  CHECK_IF(!check_type(treq.t, type), FAIL);

  alt_set_t alt_set = 0;
  type_request_t atr = REQ(t, type);
  CHECK(reduce_arg(c, 0, &alt_set, atr));
  CHECK_DELAY();
  clear_flags(c);

  *cp = mod_alt(ref(c->expr.arg[0]), ref(c->alt), 0);
  drop(c);
  return SUCCESS;

 abort:
  return abort_op(rsp, cp, treq);
}

// annotations to work around inference failures

WORD("int_t", int_t, 1, 1)
OP(int_t) {
  return func_type(cp, treq, T_INT);
}

WORD("symbol_t", symbol_t, 1, 1)
OP(symbol_t) {
  return func_type(cp, treq, T_SYMBOL);
}
