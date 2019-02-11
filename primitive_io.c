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

#include "startle/error.h"
#include "startle/support.h"
#include "startle/log.h"

#include "cells.h"
#include "rt.h"
#include "special.h"
#include "trace.h"
#include "io.h"
#include "primitive_io.h"

#if INTERFACE

typedef struct {
  file_t *(*open)   (seg_t);
  seg_t   (*read)   (file_t *);
  void    (*write)  (file_t *, seg_t);
  void    (*unread) (file_t *, seg_t);
  void    (*close)  (file_t *);
} io_t;

#endif

const io_t default_io = {
  .read = io_read,
  .write = io_write,
  .unread = io_unread,
  .open = io_open,
  .close = io_close
};

const io_t *io = &default_io;

#define WARN_ALT(op) LOG_WHEN(c->alt, MARK("WARN") " IO (" #op ") with alt")

WORD("open", open, 2, 2)
OP(open) {
  cell_t *res = 0;
  PRE(open);

  CHECK_IF(!check_type(ctx->t, T_SYMBOL), FAIL);

  CHECK(reduce_arg(c, 0, &CTX(symbol, SYM_IO)));
  CHECK(reduce_arg(c, 1, &CTX(string)));
  CHECK_IF(as_conflict(ctx->alt_set), FAIL);
  CHECK_DELAY();
  clear_flags(c);

  WARN_ALT(open);

  cell_t *p = c->expr.arg[0];
  cell_t *q = c->expr.arg[1];
  if(is_var(p) || is_var(q)) {
    res = var(T_SYMBOL, c);
    res->value.alt_set = ctx->alt_set;
    store_dep_var(c, res, 2, T_OPAQUE, ctx->alt_set);
  } else if(p->value.integer == SYM_IO) {
    void *h = io->open(value_seg(q));
    if(h) {
      store_lazy_dep(c->expr.arg[2], make_opaque(h), ctx->alt_set);
      res = mod_alt(ref(p), c->alt, ctx->alt_set);
    } else {
      ABORT(FAIL);
    }
  } else {
    ABORT(FAIL);
  }
  add_conditions(res, p, q);
  store_reduced(cp, res);
  return SUCCESS;

 abort:
  return abort_op(rsp, cp, ctx);
}

WORD("close", close, 2, 1)
OP(close) {
  cell_t *res = 0;
  PRE(close);

  CHECK_IF(!check_type(ctx->t, T_SYMBOL), FAIL);

  CHECK(reduce_arg(c, 0, &CTX(symbol, SYM_IO)));
  CHECK(reduce_arg(c, 1, &CTX(opaque)));
  CHECK_IF(as_conflict(ctx->alt_set), FAIL);
  CHECK_DELAY();
  clear_flags(c);

  WARN_ALT(close);

  cell_t *p = c->expr.arg[0], *q = c->expr.arg[1];
  if(is_var(p) || is_var(q)) {
    res = var(T_SYMBOL, c);
    res->value.alt_set = ctx->alt_set;
  } else if(p->value.integer == SYM_IO) {
    io->close(q->value.opaque);
    res = mod_alt(ref(p), c->alt, ctx->alt_set);
  } else {
    ABORT(FAIL);
  }
  add_conditions(res, p, q);
  store_reduced(cp, res);
  return SUCCESS;

 abort:
  return abort_op(rsp, cp, ctx);
}

WORD("write", write, 3, 2)
OP(write) {
  cell_t *res = 0;
  PRE(write);

  CHECK_IF(!check_type(ctx->t, T_SYMBOL), FAIL);

  CHECK(reduce_arg(c, 0, &CTX(symbol, SYM_IO)));
  CHECK(reduce_arg(c, 1, &CTX(opaque)));
  CHECK_IF(as_conflict(ctx->alt_set), FAIL);
  CHECK(reduce_arg(c, 2, &CTX(string)));
  CHECK_IF(as_conflict(ctx->alt_set), FAIL);
  CHECK_DELAY();
  clear_flags(c);

  WARN_ALT(write);

  cell_t
    *p = c->expr.arg[0],
    *q = c->expr.arg[1],
    *r = c->expr.arg[2];
  if(is_var(p) || is_var(q) || is_var(r)) {
    res = var(T_SYMBOL, c);
    res->value.alt_set = ctx->alt_set;
    store_dep_var(c, res, 3, T_OPAQUE, ctx->alt_set);
  } else if(p->value.integer == SYM_IO) {
    io->write((file_t *)q->value.opaque, value_seg(r));
    res = mod_alt(ref(p), c->alt, ctx->alt_set);
    store_lazy_dep(c->expr.arg[3], ref(q), ctx->alt_set);
  } else {
    ABORT(FAIL);
  }
  add_conditions(res, p, q, r);
  store_reduced(cp, res);
  return SUCCESS;

 abort:
  return abort_op(rsp, cp, ctx);
}

// TODO merge this with 'write'
WORD("unread", unread, 3, 2)
OP(unread) {
  cell_t *res = 0;
  PRE(unread);

  CHECK_IF(!check_type(ctx->t, T_SYMBOL), FAIL);

  CHECK(reduce_arg(c, 0, &CTX(symbol, SYM_IO)));
  CHECK(reduce_arg(c, 1, &CTX(opaque)));
  CHECK_IF(as_conflict(ctx->alt_set), FAIL);
  CHECK(reduce_arg(c, 2, &CTX(string)));
  CHECK_IF(as_conflict(ctx->alt_set), FAIL);
  CHECK_DELAY();
  clear_flags(c);

  WARN_ALT(unread);

  cell_t
    *p = c->expr.arg[0],
    *q = c->expr.arg[1],
    *r = c->expr.arg[2];
  if(is_var(p) || is_var(q) || is_var(r)) {
    res = var(T_SYMBOL, c);
    res->value.alt_set = ctx->alt_set;
    store_dep_var(c, res, 3, T_OPAQUE, ctx->alt_set);
  } else if(p->value.integer == SYM_IO) {
    io->unread(q->value.opaque, value_seg(r));
    res = mod_alt(ref(p), c->alt, ctx->alt_set);
    store_lazy_dep(c->expr.arg[3], ref(q), ctx->alt_set);
  } else {
    ABORT(FAIL);
  }
  add_conditions(res, p, q, r);
  store_reduced(cp, res);
  return SUCCESS;

 abort:
  return abort_op(rsp, cp, ctx);
}

WORD("read", read, 2, 3)
OP(read) {
  cell_t *res = 0;
  PRE(read);

  CHECK_IF(!check_type(ctx->t, T_SYMBOL), FAIL);

  CHECK(reduce_arg(c, 0, &CTX(symbol, SYM_IO)));
  CHECK(reduce_arg(c, 1, &CTX(opaque)));
  CHECK_IF(as_conflict(ctx->alt_set), FAIL);
  CHECK_DELAY();
  clear_flags(c);

  WARN_ALT(read);

  cell_t *p = c->expr.arg[0];
  cell_t *q = c->expr.arg[1];
  if(is_var(p) || is_var(q)) {
    res = var(T_SYMBOL, c);
    res->value.alt_set = ctx->alt_set;
    store_dep_var(c, res, 2, T_OPAQUE, ctx->alt_set);
    store_dep_var(c, res, 3, T_STRING, ctx->alt_set);
  } else if(p->value.integer == SYM_IO) {
    seg_t s = io->read((file_t *)q->value.opaque);
    store_lazy_dep(c->expr.arg[2], ref(q), ctx->alt_set);
    store_lazy_dep(c->expr.arg[3], make_string(s), ctx->alt_set);
    res = mod_alt(ref(p), c->alt, ctx->alt_set);
  } else {
    ABORT(FAIL);
  }
  add_conditions(res, p, q);
  store_reduced(cp, res);
  return SUCCESS;

 abort:
  return abort_op(rsp, cp, ctx);
}

/* Local Variables: */
/* eval: (add-to-list 'imenu-generic-expression '("Operator" "^.*OP(\\([a-z_]+\\)).*$" 1)) */
/* End: */