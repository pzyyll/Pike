/*
 * $Id: md2.c,v 1.6 1999/02/10 21:51:37 hubbe Exp $
 *
 * A pike module for getting access to some common cryptos.
 *
 * /precompiled/crypto/md2
 *
 * Henrik Grubbström 1996-10-24
 */

/*
 * Includes
 */

/* From the Pike distribution */
#include "global.h"
#include "stralloc.h"
#include "interpret.h"
#include "svalue.h"
#include "constants.h"
#include "pike_macros.h"
#include "threads.h"
#include "object.h"
#include "stralloc.h"
#include "builtin_functions.h"

/* System includes */
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

/* SSL includes */
#include <md2.h>

/* Module specific includes */
#include "precompiled_crypto.h"

/*
 * Globals
 */

struct program *pike_md2_program;

/*
 * Functions
 */

static void init_pike_md2(struct object *o)
{
  MD2_Init(&(PIKE_MD2->ctx));
  PIKE_MD2->string = NULL;
}

static void exit_pike_md2(struct object *o)
{
  if (!PIKE_MD2->string) {
    MD2_Final(PIKE_MD2->checksum, &(PIKE_MD2->ctx));
  } else {
    free_string(PIKE_MD2->string);
    PIKE_MD2->string = NULL;
  }
}

/*
 * efuns and the like
 */

/* void push(string) */
static void f_push(INT32 args)
{
  if (args != 1) {
    error("Wrong number of arguments to push()\n");
  }
  if (sp[-1].type != T_STRING) {
    error("Bad argument 1 to push()\n");
  }
  if (!PIKE_MD2->string) {
    MD2_CTX *c = &(PIKE_MD2->ctx);

    MD2_Update(c,
	       (unsigned char *)sp[-1].u.string->str,
	       (unsigned long)sp[-1].u.string->len);
  } else {
    error("md2->push(): Object not active\n");
  }

  pop_n_elems(args);
}

/* string cast_to_string(void) */
static void f_cast_to_string(INT32 args)
{
  if (args) {
    error("Too many arguments to md2->cast_to_string()\n");
  }
  if (!PIKE_MD2->string) {
    MD2_Final(PIKE_MD2->checksum, &(PIKE_MD2->ctx));

    PIKE_MD2->string = make_shared_binary_string((char *)PIKE_MD2->checksum,
						 MD2_DIGEST_LENGTH);
    add_ref(PIKE_MD2->string);
  }

  push_string(PIKE_MD2->string);
}

/* mixed cast(string) */
static void f_cast(INT32 args)
{
  if (args != 1) {
    error("Wrong number of arguments to md2->cast()\n");
  }
  if (sp[-1].type != T_STRING) {
    error("Bad arguemnt 1 to md2->cast()\n");
  }
  if (strcmp(sp[-1].u.string->str, "string")) {
    error("md2->cast(): Only casts to string supported.\n");
  }
  pop_n_elems(args);

  f_cast_to_string(0);
}

/*
 * Module linkage
 */

void init_md2_efuns(void)
{
  /* 
/* function(string:void) */
  ADD_EFUN()s */
}

void init_md2_programs(void)
{
  /*
   * start_new_program();
   *
   * add_storage();
   *
   * add_function();
   * add_function();
   * ...
   *
   * set_init_callback();
   * set_exit_callback();
   *
   * program = end_c_program();
   * program->refs++;
   *
   */

  /* /precompiled/crypto/md2 */
  start_new_program();
  ADD_STORAGE(struct pike_md2);

  add_function("push", f_push,tFunc(tStr,tVoid), 0);
  /* function(string:mixed) */
  ADD_FUNCTION("cast", f_cast,tFunc(tStr,tMix), 0);
  /* function(void:string) */
  ADD_FUNCTION("cast_to_string", f_cast_to_string,tFunc(tVoid,tStr), 0);

  set_init_callback(init_pike_md2);
  set_exit_callback(exit_pike_md2);

  add_ref(pike_md2_program = end_c_program("/precompiled/crypto/md2"));
}

void exit_md2(void)
{
  /* free_program()s */
  free_program(pike_md2_program);
}
