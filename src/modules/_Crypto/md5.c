/*
 * $Id: md5.c,v 1.1.1.1 1996/11/05 15:10:09 grubba Exp $
 *
 * A pike module for getting access to some common cryptos.
 *
 * /precompiled/crypto/md5
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
#include "macros.h"
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
#include <md5.h>

/* Module specific includes */
#include "precompiled_crypto.h"

/*
 * Globals
 */

struct program *pike_md5_program;

/*
 * Functions
 */

static void init_pike_md5(struct object *o)
{
  MD5_Init(&(PIKE_MD5->ctx));
  PIKE_MD5->string = NULL;
}

static void exit_pike_md5(struct object *o)
{
  if (!PIKE_MD5->string) {
    MD5_Final(PIKE_MD5->checksum, &(PIKE_MD5->ctx));
  } else {
    free_string(PIKE_MD5->string);
    PIKE_MD5->string = NULL;
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
  if (!PIKE_MD5->string) {
    MD5_CTX *c = &(PIKE_MD5->ctx);

    MD5_Update(c,
	       (unsigned char *)sp[-1].u.string->str,
	       (unsigned long)sp[-1].u.string->len);
  } else {
    error("md5->push(): Object not active\n");
  }

  pop_n_elems(args);
}

/* string cast_to_string(void) */
static void f_cast_to_string(INT32 args)
{
  if (args) {
    error("Too many arguments to md5->cast_to_string()\n");
  }
  if (!PIKE_MD5->string) {
    MD5_Final(PIKE_MD5->checksum, &(PIKE_MD5->ctx));

    PIKE_MD5->string = make_shared_binary_string((char *)PIKE_MD5->checksum,
						 MD5_DIGEST_LENGTH);
    PIKE_MD5->string->refs++;
  }

  push_string(PIKE_MD5->string);
  PIKE_MD5->string->refs++;
}

/* mixed cast(string) */
static void f_cast(INT32 args)
{
  if (args != 1) {
    error("Wrong number of arguments to md5->cast()\n");
  }
  if (sp[-1].type != T_STRING) {
    error("Bad arguemnt 1 to md5->cast()\n");
  }
  if (strcmp(sp[-1].u.string->str, "string")) {
    error("md5->cast(): Only casts to string supported.\n");
  }
  pop_n_elems(args);

  f_cast_to_string(0);
}

/*
 * Module linkage
 */

void init_md5_efuns(void)
{
  /* add_efun()s */
}

void init_md5_programs(void)
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

  /* /precompiled/crypto/md5 */
  start_new_program();
  add_storage(sizeof(struct pike_md5));

  add_function("push", f_push, "function(string:void)", OPT_SIDE_EFFECT);
  add_function("cast", f_cast, "function(string:mixed)", OPT_EXTERNAL_DEPEND);
  add_function("cast_to_string", f_cast_to_string, "function(void:string)", OPT_EXTERNAL_DEPEND);

  set_init_callback(init_pike_md5);
  set_exit_callback(exit_pike_md5);

  pike_md5_program = end_c_program("/precompiled/crypto/md5");
  pike_md5_program->refs++;
}

void exit_md5(void)
{
  /* free_program()s */
  free_program(pike_md5_program);
}
