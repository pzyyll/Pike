/*\
||| This file a part of Pike, and is copyright by Fredrik Hubinette
||| Pike is distributed as GPL (General Public License)
||| See the files COPYING and DISCLAIMER for more information.
\*/

/*
 * $Id: constants.h,v 1.8 1999/02/10 21:46:40 hubbe Exp $
 */
#ifndef ADD_EFUN_H
#define ADD_EFUN_H

#include "svalue.h"
#include "hashtable.h"
#include "las.h" /* For OPT_SIDE_EFFECT etc. */
#include "block_alloc_h.h"

typedef int (*docode_fun)(node *n);
typedef node *(*optimize_fun)(node *n);

struct callable
{
  INT32 refs;
#ifdef PIKE_SECURITY
  struct object *prot;
#endif
  c_fun function;
  struct pike_string *type;
  struct pike_string *name;
  INT16 flags;
  optimize_fun optimize;
  docode_fun docode;
  struct callable *next;
};

/* Prototypes begin here */
struct mapping *get_builtin_constants(void);
void low_add_efun(struct pike_string *name, struct svalue *fun);
void low_add_constant(char *name, struct svalue *fun);
void add_global_program(char *name, struct program *p);
BLOCK_ALLOC(callable,128)
struct callable *low_make_callable(c_fun fun,
				   struct pike_string *name,
				   struct pike_string *type,
				   INT16 flags,
				   optimize_fun optimize,
				   docode_fun docode);
struct callable *make_callable(c_fun fun,
			       char *name,
			       char *type,
			       INT16 flags,
			       optimize_fun optimize,
			       docode_fun docode);
void add_efun2(char *name,
	       c_fun fun,
	       char *type,
	       INT16 flags,
	       optimize_fun optimize,
	       docode_fun docode);
void add_efun(char *name, c_fun fun, char *type, INT16 flags);
void quick_add_efun(char *name, int name_length,
		    c_fun fun,
		    char *type, int type_length,
		    INT16 flags,
		    optimize_fun optimize,
		    docode_fun docode);
void cleanup_added_efuns(void);
void count_memory_in_callables(INT32 *num_, INT32 *size_);
/* Prototypes end here */


#include "pike_macros.h"

#define ADD_EFUN(NAME,FUN,TYPE,FLAGS) \
    quick_add_efun(NAME,CONSTANT_STRLEN(NAME),FUN,TYPE,CONSTANT_STRLEN(TYPE),FLAGS,0,0)

#define ADD_EFUN2(NAME,FUN,TYPE,FLAGS,OPTIMIZE,DOCODE) \
    quick_add_efun(NAME,CONSTANT_STRLEN(NAME),FUN,TYPE,CONSTANT_STRLEN(TYPE),FLAGS,OPTIMIZE,DOCODE)
#endif
