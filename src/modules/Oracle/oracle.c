/*
 * $Id: oracle.c,v 1.16 1999/02/10 21:49:57 hubbe Exp $
 *
 * Pike interface to Oracle databases.
 *
 * Marcus Comstedt
 */

/*
 * Includes
 */

#include "global.h"

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif /* HAVE_CONFIG_H */

#include "svalue.h"
#include "object.h"
#include "array.h"
#include "stralloc.h"
#include "interpret.h"
#include "pike_types.h"
#include "pike_memory.h"
#include "threads.h"
#include "module_support.h"
#include "mapping.h"
#include "multiset.h"
#include "builtin_functions.h"

#ifdef HAVE_ORACLE

#include <ocidfn.h>
#include <ociapr.h>

#endif

RCSID("$Id: oracle.c,v 1.16 1999/02/10 21:49:57 hubbe Exp $");

#ifdef HAVE_ORACLE

#define BLOB_FETCH_CHUNK 16384

#undef THREADS_ALLOW
#define THREADS_ALLOW()
#undef THREADS_DISALLOW
#define THREADS_DISALLOW()

struct program *oracle_program = NULL, *oracle_result_program = NULL;

struct dbcurs {
  struct dbcurs *next;
  Cda_Def cda;
};

struct dbcon {
  Lda_Def lda;
  ub4 hda[128];
  struct dbcurs *cdas, *share_cda;
};

struct dbresult {
  struct object *parent;
  struct dbcon *dbcon;
  struct dbcurs *curs;
  Cda_Def *cda;
  INT32 cols;
};


static void error_handler(struct dbcon *dbcon, sword rc)
{
  static text msgbuf[512];
  oerhms(&dbcon->lda, rc, msgbuf, sizeof(msgbuf));
  error(msgbuf);
}


#define THIS ((struct dbresult *)(fp->current_storage))

static void init_dbresult_struct(struct object *o)
{
  memset(THIS, 0, sizeof(*THIS));
}

static void exit_dbresult_struct(struct object *o)
{
  struct dbresult *r = THIS;

  if(r->curs) {
    ocan(&r->curs->cda);
    r->curs->next = r->dbcon->cdas;
    r->dbcon->cdas = r->curs;
  }

  if(r->parent)
    free_object(r->parent);
}

static void f_result_create(INT32 args)
{
  struct object *p;
  struct dbcon *dbcon;
  struct dbcurs *curs;
  struct dbresult *r = THIS;
  INT32 i;

  get_all_args("Oracle.oracle_result->create", args, "%o", &p);
  
  if(!(dbcon = (struct dbcon *)get_storage(p, oracle_program)) ||
     !(curs = dbcon->share_cda))
    error("Bad argument 1 to Oracle.oracle_result->create()\n");

  r->curs = curs;
  dbcon->share_cda = NULL;

  add_ref(r->parent = p);
  r->dbcon = dbcon;
  r->cda = &curs->cda;

  r->cols = 0;
}

static void f_num_fields(INT32 args)
{
  struct dbresult *r = THIS;

  if(THIS->cols == 0) {
    sword rc;
    INT32 i;
    sb4 siz;

    THREADS_ALLOW();

    for(i=11; ; i+=10)
      if((rc = odescr(r->cda, i, &siz, NULL, NULL, NULL, NULL,
		      NULL, NULL, NULL)))
	break;

    if(r->cda->rc == 1007)
      for(i-=10; ; i++)
	if((rc = odescr(r->cda, i, &siz, NULL, NULL, NULL, NULL,
			NULL, NULL, NULL)))
	  break;

    THREADS_DISALLOW();    

    if(r->cda->rc != 1007)
      error_handler(r->dbcon, r->cda->rc);

    THIS->cols = i-1;
  }
  pop_n_elems(args);
  push_int(THIS->cols);
}

static void f_fetch_fields(INT32 args)
{
  struct dbresult *r = THIS;
  INT32 i, nambufsz=64;
  sword rc;
  text *nambuf=xalloc(nambufsz+1);

  pop_n_elems(args);

  for(i=0; i<r->cols || r->cols == 0; i++) {

    sb4 siz, cbufl, dispsz;
    sb2 typ, scale;

    for(;;) {

      cbufl = nambufsz;
      
      THREADS_ALLOW();
      
      rc = odescr(r->cda, i+1, &siz, &typ, nambuf, &cbufl, &dispsz,
		  NULL, &scale, NULL);
      
      THREADS_DISALLOW();

      if(rc || cbufl < nambufsz) break;

      free(nambuf);
      nambuf = xalloc((nambufsz <<= 1)+1);

    }

    if(rc) {
      if(r->cda->rc == 1007)
	break;
      free(nambuf);
      error_handler(r->dbcon, r->cda->rc);
    }

    push_text("name");
    push_string(make_shared_binary_string(nambuf, cbufl));
    push_text("type");
    switch(typ) {
    case SQLT_CHR:
      push_text("varchar2");
      break;
    case SQLT_NUM:
      push_text("number");
      break;
    case SQLT_LNG:
      push_text("long");
      break;
    case SQLT_RID:
      push_text("rowid");
      break;
    case SQLT_DAT:
      push_text("date");
      break;
    case SQLT_BIN:
      push_text("raw");
      break;
    case SQLT_LBI:
      push_text("long raw");
      break;
    case SQLT_AFC:
      push_text("char");
      break;
    case SQLT_LAB:
      push_text("mslabel");
      break;
    default:
      push_int(0);
      break;
    }
    push_text("length");
    push_int(dispsz);
    push_text("decimals");
    push_int(scale);
    f_aggregate_mapping(8);
  }

  free(nambuf);

  if(r->cols == 0)
    r->cols = i;

  f_aggregate(r->cols);
}

static void f_fetch_row(INT32 args)
{
  struct fetchslot {
    struct fetchslot *next;
    INT32 siz;
    ub2 rsiz, rcode;
    sb2 indp;
    sword typ;
    char data[1];
  } *s, *s2, *slots = NULL;
  struct dbresult *r = THIS;
  sword rc;
  INT32 i;

  pop_n_elems(args);

  for(i=0; i<r->cols || r->cols == 0; i++) {
    sb4 siz, dsiz;
    sb2 typ;

    THREADS_ALLOW();

    rc = odescr(r->cda, i+1, &siz, &typ, NULL, NULL, &dsiz,
		NULL, NULL, NULL);

    THREADS_DISALLOW();    

    if(rc) {
      if(r->cda->rc == 1007)
	break;
      while((s=slots)) {
	slots=s->next;
	free(s);
      }
      error_handler(r->dbcon, r->cda->rc);
    }
    
    s = (struct fetchslot *)xalloc(sizeof(struct fetchslot)+dsiz+4);

    s->next = slots;
    s->siz = dsiz+4;
    slots = s;

    THREADS_ALLOW();

    s->rcode = 0;
    s->indp = -1;

    rc = odefin(r->cda, i+1, s->data, s->siz,
		s->typ=((typ==SQLT_LNG || typ==SQLT_BIN || typ==SQLT_LBI)?
			typ : SQLT_STR),
		-1, &s->indp, NULL, -1, -1, &s->rsiz, &s->rcode);

    THREADS_DISALLOW();

    if(rc) {
      while((s=slots)) {
	slots=s->next;
	free(s);
      }
      error_handler(r->dbcon, r->cda->rc);
    }
  }

  if(r->cols == 0)
    r->cols = i;

  /* Do link reversal */
  for(s=NULL; slots; slots=s2) {
    s2 = slots->next;
    slots->next = s;
    s = slots;
  }
  slots = s;

  THREADS_ALLOW();

  rc = ofetch(r->cda);

  THREADS_DISALLOW();

  if(rc) {
    while((s=slots)) {
      slots=s->next;
      free(s);
    }
    if(r->cda->rc == 1403) {
      push_int(0);
      return;
    } else error_handler(r->dbcon, r->cda->rc);
  }

  for(s=slots, i=0; i<r->cols; i++) {
    if(s->indp == -1)
      push_int(0);
    else if(s->rcode == 1406 && (s->typ == SQLT_LNG || s->typ == SQLT_LBI)) {
      sb4 retl, offs=0;
      sb1 *buf = xalloc(BLOB_FETCH_CHUNK);
      struct pike_string *s1=make_shared_binary_string("", 0), *s2, *s3;
      for(;;) {

	retl=0;
	oflng(r->cda, i+1, buf, BLOB_FETCH_CHUNK, s->typ, &retl, offs);
	if(!retl)
	  break;

	s3 = add_shared_strings(s1, s2=make_shared_binary_string(buf, retl));
	free_string(s1);
	free_string(s2);
	s1 = s3;
	offs += retl;
      }
      free(buf);
      push_string(s1);
    } else if(s->rcode)
      push_int(0);
    else
      push_string(make_shared_binary_string(s->data, s->rsiz));
    s2=s->next;
    free(s);
    s=s2;
  }

  f_aggregate(r->cols);
}

#undef THIS
#define THIS ((struct dbcon *)(fp->current_storage))

static void init_dbcon_struct(struct object *o)
{
  memset(THIS, 0, sizeof(*THIS));
}

static void exit_dbcon_struct(struct object *o)
{
  struct dbcon *dbcon = THIS;
  struct dbcurs *curs;
  ologof(&dbcon->lda);
  while((curs = dbcon->cdas) != NULL) {
    dbcon->cdas = curs->next;
    free(curs);
  }
}

static struct dbcurs *make_cda(struct dbcon *dbcon)
{
  sword rc;
  struct dbcurs *curs=(struct dbcurs *)xalloc(sizeof(struct dbcurs));

  memset(curs, 0, sizeof(*curs));

  THREADS_ALLOW();

  rc=oopen(&curs->cda, &dbcon->lda, NULL, -1, -1, NULL, -1);

  THREADS_DISALLOW();

  if(rc) {
    rc = curs->cda.rc;
    free(curs);
    error_handler(dbcon, rc);
  } else {
    curs->next = dbcon->cdas;
    dbcon->cdas = curs;
  }
  return curs;
}

static void f_create(INT32 args)
{
  struct dbcon *dbcon = THIS;
  struct pike_string *uid, *passwd, *host, *database;
  sword rc;

  check_all_args("Oracle.oracle->create", args,
		 BIT_STRING|BIT_INT, BIT_STRING|BIT_INT, BIT_STRING,
		 BIT_STRING|BIT_VOID|BIT_INT, 0);

  host = (sp[-args].type == T_STRING? sp[-args].u.string : NULL);
  database = (sp[1-args].type == T_STRING? sp[1-args].u.string : NULL);
  uid = (sp[2-args].type == T_STRING? sp[2-args].u.string : NULL);
  if(args >= 4)
    passwd = (sp[3-args].type == T_STRING? sp[3-args].u.string : NULL);
  else
    passwd = NULL;

  THREADS_ALLOW();

  rc = olog(&dbcon->lda, (ub1*)dbcon->hda, uid->str, uid->len,
	    (passwd? passwd->str:NULL), (passwd? passwd->len:-1),
	    (host? host->str:NULL), (host? host->len:-1),
	    OCI_LM_DEF);

  THREADS_DISALLOW();

  if(rc)
    error_handler(dbcon, dbcon->lda.rc);

  make_cda(dbcon);

  pop_n_elems(args);
  return;
}

static void f_big_query(INT32 args)
{
  struct pike_string *query;
  struct dbcurs *curs;
  sword rc;
  struct mapping *bnds;
  /*  INT32 cols=0; */

  if(args>1)
    get_all_args("Oracle.oracle->big_query", args, "%S%m", &query, &bnds);
  else {
    get_all_args("Oracle.oracle->big_query", args, "%S", &query);
    bnds = NULL;
  }

  if(!(curs = THIS->cdas))
    curs = make_cda(THIS);

  THIS->cdas = curs->next;

  THREADS_ALLOW();

  ocan(&curs->cda);

  rc = oparse(&curs->cda, query->str, query->len, 1, 2);
 
  THREADS_DISALLOW();
   
  if(rc) {
    curs->next = THIS->cdas;
    THIS->cdas = curs;
    error_handler(THIS, curs->cda.rc);
  } else if(bnds != NULL) {
    INT32 e;
    struct keypair *k;
    MAPPING_LOOP(bnds) {
	sword rc = 0;
	ub1 *addr;
	sword len, fty;
	switch(k->val.type) {
	case T_STRING:
	  addr = (ub1 *)k->val.u.string->str;
	  len = k->val.u.string->len;
	  fty = SQLT_CHR;
	  break;
	case T_FLOAT:
	  addr = (ub1 *)&k->val.u.float_number;
	  len = sizeof(k->val.u.float_number);
	  fty = SQLT_FLT;
	  break;
	case T_INT:
	  addr = (ub1 *)&k->val.u.integer;
	  len = sizeof(k->val.u.integer);
	  fty = SQLT_INT;
	  break;
	case T_MULTISET:
	  if(k->val.u.multiset->ind->size == 1 &&
	     ITEM(k->val.u.multiset->ind)[0].type == T_STRING) {
	    addr = (ub1 *)ITEM(k->val.u.multiset->ind)[0].u.string->str;
	    len = ITEM(k->val.u.multiset->ind)[0].u.string->len;
	    fty = SQLT_BIN;
	    break;
	  }
	default:
	  ocan(&curs->cda);
	  curs->next = THIS->cdas;
	  THIS->cdas = curs;
	  error("Bad value type in argument 2 to "
		"Oracle.oracle->big_query()\n");
	}
	if(k->ind.type == T_INT)
	  rc = obndrn(&curs->cda, k->ind.u.integer, addr, len, fty,
		      -1, NULL, NULL, -1, 0);
	else if(k->ind.type == T_STRING)
	  rc = obndrv(&curs->cda, k->ind.u.string->str, k->ind.u.string->len,
		      addr, len, fty, -1, NULL, NULL, -1, 0);
	else {
	  ocan(&curs->cda);
	  curs->next = THIS->cdas;
	  THIS->cdas = curs;
	  error("Bad index type in argument 2 to "
		"Oracle.oracle->big_query()\n");
	}
	if(rc) {
	  rc = curs->cda.rc;
	  ocan(&curs->cda);
	  curs->next = THIS->cdas;
	  THIS->cdas = curs;
	  error_handler(THIS, rc);
	}
      }
  }

  THREADS_ALLOW();

  rc = oexec(&curs->cda);

  THREADS_DISALLOW();

  if(rc) {
    rc = curs->cda.rc;
    ocan(&curs->cda);
    curs->next = THIS->cdas;
    THIS->cdas = curs;
    error_handler(THIS, rc);
  }

  pop_n_elems(args);

  if(curs->cda.ft != 4) {
    ocan(&curs->cda);
    curs->next = THIS->cdas;
    THIS->cdas = curs;
    push_int(0);
    return;
  }

  push_object(this_object());

  /*

  for(;;) {
    text cbuf[32];
    sb4 siz, cbufl=sizeof(cbuf), dispsz;
    sb2 typ, prec, scale, nullok;

    THREADS_ALLOW();

    rc = odescr(&curs->cda, cols+1, &siz, &typ, cbuf, &cbufl, &dispsz,
		&prec, &scale, &nullok);

    THREADS_DISALLOW();

    if(rc) {
      if((rc = curs->cda.rc) == 1007)
	break;
      ocan(&curs->cda);
      curs->next = THIS->cdas;
      THIS->cdas = curs;
      error_handler(THIS, rc);
    }
    push_string(make_shared_binary_string(cbuf, cbufl));
    push_int(typ);
    cols++;
  }

  */

  curs->next = THIS->cdas;
  THIS->cdas = curs;

  /*
  if(!cols) {
    ocan(&curs->cda);
    pop_n_elems(1);
    push_int(0);
    return;
  }

  f_aggregate(cols*2);
  */

  THIS->share_cda = curs;

  push_object(clone_object(oracle_result_program, 1));

  THIS->cdas = curs->next;

}

static void f_list_tables(INT32 args)
{
  struct pike_string *wild;
  struct dbcurs *curs;
  sword rc;

  if(args)
    get_all_args("Oracle.oracle->list_tables", args, "%S", &wild);
  else
    wild = NULL;

  if(!(curs = THIS->cdas))
    curs = make_cda(THIS);

  THIS->cdas = curs->next;

  THREADS_ALLOW();

  ocan(&curs->cda);

  if(wild) {
    rc = oparse(&curs->cda, "select tname from tab where tname like :wild",
		-1, 1, 2);
    if(!rc)
      rc = obndrv(&curs->cda, ":wild", -1, wild->str, wild->len, SQLT_CHR,
		  -1, NULL, NULL, -1, 0);
  } else
    rc = oparse(&curs->cda, "select tname from tab", -1, 1, 2);
 
  THREADS_DISALLOW();
   
  if(rc) {
    curs->next = THIS->cdas;
    THIS->cdas = curs;
    error_handler(THIS, curs->cda.rc);
  }

  THREADS_ALLOW();

  rc = oexec(&curs->cda);

  THREADS_DISALLOW();

  if(rc) {
    rc = curs->cda.rc;
    ocan(&curs->cda);
    curs->next = THIS->cdas;
    THIS->cdas = curs;
    error_handler(THIS, rc);
  }

  pop_n_elems(args);

  push_object(this_object());

  curs->next = THIS->cdas;
  THIS->cdas = curs;
  THIS->share_cda = curs;

  push_object(clone_object(oracle_result_program, 1));

  THIS->cdas = curs->next;

}

#endif

void pike_module_init(void)
{
#ifdef HAVE_ORACLE

#ifdef ORACLE_HOME
  if(getenv("ORACLE_HOME")==NULL)
    putenv("ORACLE_HOME="ORACLE_HOME);
#endif
#ifdef ORACLE_SID
  if(getenv("ORACLE_SID")==NULL)
    putenv("ORACLE_SID="ORACLE_SID);
#endif

  /*  opinit(OCI_EV_TSF); */

  start_new_program();
  ADD_STORAGE(struct dbcon);

  /* function(string|void, string|void, string|void, string|void:void) */
  ADD_FUNCTION("create", f_create,tFunc(tOr(tStr,tVoid) tOr(tStr,tVoid) tOr(tStr,tVoid) tOr(tStr,tVoid),tVoid), ID_PUBLIC);
  /* function(string,mapping(int|string:int|float|string|multiset(string))|void:object) */
  ADD_FUNCTION("big_query", f_big_query,tFunc(tStr tOr(tMap(tOr(tInt,tStr),tOr4(tInt,tFlt,tStr,tSet(tStr))),tVoid),tObj), ID_PUBLIC);
  /* function(void|string:object) */
  ADD_FUNCTION("list_tables", f_list_tables,tFunc(tOr(tVoid,tStr),tObj), ID_PUBLIC);

  set_init_callback(init_dbcon_struct);
  set_exit_callback(exit_dbcon_struct);

  oracle_program = end_program();
  add_program_constant("oracle", oracle_program, 0);

  start_new_program();
  ADD_STORAGE(struct dbresult);

  /* function(object, array(string|int):void) */
  ADD_FUNCTION("create", f_result_create,tFunc(tObj tArr(tOr(tStr,tInt)),tVoid), ID_PUBLIC);
  /* function(:int) */
  ADD_FUNCTION("num_fields", f_num_fields,tFunc(,tInt), ID_PUBLIC);
  /* function(:array(mapping(string:mixed))) */
  ADD_FUNCTION("fetch_fields", f_fetch_fields,tFunc(,tArr(tMap(tStr,tMix))), ID_PUBLIC);
  /* function(:int|array(string|int)) */
  ADD_FUNCTION("fetch_row", f_fetch_row,tFunc(,tOr(tInt,tArr(tOr(tStr,tInt)))), ID_PUBLIC);

  set_init_callback(init_dbresult_struct);
  set_exit_callback(exit_dbresult_struct);

  oracle_result_program = end_program();
#endif
}

static void call_atexits(void);

void pike_module_exit(void)
{
#ifdef HAVE_ORACLE
  if(oracle_program)
  {
    free_program(oracle_program);
    oracle_program = NULL;
  }
  if (oracle_result_program) {
    free_program(oracle_result_program);
    oracle_result_program = NULL;
  }
#endif
  call_atexits();
}

#ifdef DYNAMIC_MODULE

static int atexit_cnt=0;
static void (*atexit_fnc[32])(void);

int atexit(void (*func)(void))
{
  if(atexit_cnt==32)
    return -1;
  atexit_fnc[atexit_cnt++]=func;
  return 0;
}

static void call_atexits(void)
{
  while(atexit_cnt)
    (*atexit_fnc[--atexit_cnt])();
}

#else

static void call_atexits(void)
{
}

#endif

