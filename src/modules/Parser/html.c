#include "global.h"
#include "config.h"

#include "pike_macros.h"
#include "object.h"
#include "constants.h"
#include "interpret.h"
#include "svalue.h"
#include "threads.h"
#include "array.h"
#include "error.h"
#include "operators.h"
#include "builtin_functions.h"
#include "module_support.h"
#include "mapping.h"

#include "parser.h"

#ifdef DEBUG
#undef DEBUG
#endif
#if 1
#define DEBUG(X) fprintf X
#else
#define DEBUG(X) do; while(0)
#endif

#if 0
#define free(X) fprintf(stderr,"free line %d: %p\n",__LINE__,X); free(X)
#endif

#define MAX_FEED_STACK_DEPTH 10

/*
**! module Parser
**! class HTML
*/

struct location
{
   int byteno;     /* current byte, first=1 */
   int lineno;     /* line number, first=1 */
   int linestart;  /* byte current line started at */
};

struct piece
{
   struct pike_string *s;
   struct piece *next;
};

struct feed_stack
{
   int ignore_data;
   
   struct feed_stack *prev;

   /* this is a feed-stack, ie
      these contains the result of callbacks,
      if they are to be parsed.
      
      The bottom stack element has no local feed,
      it's just for convinience */

   /* current position; if not local feed, use global feed */
   struct piece *local_feed;
   int c; 

   struct location pos;
};

struct parser_html_storage
{
/*--- current state -----------------------------------------------*/

   /* feeded info */
   struct piece *feed,*feed_end;

   /* resulting data */
   struct piece *out,*out_end;
   
   /* parser stack */
   struct feed_stack *stack;
   int stack_count;
   int max_stack_depth;

   /* current range (ie, current tag/entity/whatever) */
   struct piece *start,*end;
   int cstart,cend;

/*--- user configurable -------------------------------------------*/

   /* callback functions */
   struct svalue callback__tag;
   struct svalue callback__data;
   struct svalue callback__entity;

   /* arg quote may have tag_end to end quote and tag */
   int lazy_end_arg_quote; 

   p_wchar2 tag_start,tag_end;
   p_wchar2 entity_start,entity_end;
   int nargq;
#define MAX_ARGQ 8
   p_wchar2 argq_start[MAX_ARGQ],argq_stop[MAX_ARGQ];

   /* pre-calculated */
   /* end of tag or start of arg quote */
   p_wchar2 look_for_start[MAX_ARGQ+2];
   int num_look_for_start;

   /* end(s) of _this_ arg quote */
   p_wchar2 look_for_end[MAX_ARGQ][MAX_ARGQ+2];
   int num_look_for_end[MAX_ARGQ];
};

#ifdef THIS
#undef THIS /* Needed for NT */
#endif

#define THIS ((struct parser_html_storage*)(fp->current_storage))
#define THISOBJ (fp->current_object)

static struct pike_string *empty_string;

static p_wchar2 whitespace[]={' ','\n','\r','\t','\v'};
static p_wchar2 ws_or_endarg[]={' ','\n','\r','\t','\v','>','='};

/****** init & exit *********************************/

void reset_feed(struct parser_html_storage *this)
{
   struct piece *f;
   struct feed_stack *st;

   /* kill feed */

   while (this->feed)
   {
      f=this->feed;
      this->feed=f->next;
      free_string(f->s);
      free(f);
   }
   this->feed_end=NULL;

   /* kill out-feed */

   while (this->out)
   {
      f=this->out;
      this->out=f->next;
      free_string(f->s);
      free(f);
   }
   this->out_end=NULL;


   /* free stack */

   while (this->stack)
   {
      st=this->stack;
      this->stack=st->prev;
      free(st);
   }

   /* new stack head */

   this->stack=malloc(sizeof(struct feed_stack));
   if (!this->stack)
      error("out of memory\n");
   this->stack->prev=NULL;
   this->stack->local_feed=NULL;
   this->stack->ignore_data=0;
   this->stack->pos.byteno=1;
   this->stack->pos.lineno=1;
   this->stack->pos.linestart=1;
   this->stack->c=0;

   this->stack_count=0;
}

static void recalculate_argq(struct parser_html_storage *this)
{
   int n,i,j,k;

   /* prepare look for start of argument quote or end of tag */
   this->look_for_start[0]=this->tag_end;
   n=1;
   for (i=0; i<this->nargq; i++)
   {
      for (j=0; j<n; j++)
	 if (this->look_for_start[j]==this->argq_start[i]) goto found_start;
      this->look_for_start[n++]=this->argq_start[i];
found_start:
      ;
   }
   this->num_look_for_start=n;

   for (k=0; k<this->nargq; k++)
   {
      n=0;
      for (i=0; i<this->nargq; i++)
	 if (this->argq_start[k]==this->argq_start[i])
	 {
	    for (j=0; j<this->nargq; j++)
	       if (this->look_for_end[k][j]==this->argq_start[i])
		  goto found_end;
	    this->look_for_end[k][n++]=this->argq_start[i];
   found_end:
	    ;
	 }
      if (this->lazy_end_arg_quote)
	 this->look_for_end[k][n++]=this->tag_end;

      this->num_look_for_end[k]=n;
   }
}

static void init_html_struct(struct object *o)
{
   DEBUG((stderr,"init_html_struct %p\n",THIS));

   /* default set */
   THIS->tag_start='<';
   THIS->tag_end='>';
   THIS->entity_start='&';
   THIS->entity_end=';';
   THIS->nargq=2;
   THIS->argq_start[0]='\"';
   THIS->argq_stop[0]='\"';
   THIS->argq_start[1]='\'';
   THIS->argq_stop[1]='\'';

   THIS->lazy_end_arg_quote=0;

   recalculate_argq(THIS);

   /* initialize feed */
   THIS->feed=NULL;
   THIS->out=NULL;
   THIS->stack=NULL;
   reset_feed(THIS);
   
   /* clear callbacks */
   THIS->callback__tag.type=T_INT;
   THIS->callback__data.type=T_INT;
   THIS->callback__entity.type=T_INT;

   /* settings */
   THIS->max_stack_depth=MAX_FEED_STACK_DEPTH;
}

static void exit_html_struct(struct object *o)
{
   DEBUG((stderr,"exit_html_struct %p\n",THIS));

   reset_feed(THIS); /* frees feed & out */

   free_svalue(&(THIS->callback__tag));
   free_svalue(&(THIS->callback__data));
   free_svalue(&(THIS->callback__entity));
   free(THIS->stack);
}

/****** setup callbacks *****************************/

static void html__set_tag_callback(INT32 args)
{
   if (!args) error("_set_tag_callback: too few arguments\n");
   assign_svalue(&(THIS->callback__tag),sp-args);
   pop_n_elems(args);
   push_int(0);
}

static void html__set_data_callback(INT32 args)
{
   if (!args) error("_set_data_callback: too few arguments\n");
   assign_svalue(&(THIS->callback__data),sp-args);
   pop_n_elems(args);
   push_int(0);
}

static void html__set_entity_callback(INT32 args)
{
   if (!args) error("_set_entity_callback: too few arguments\n");
   assign_svalue(&(THIS->callback__entity),sp-args);
   pop_n_elems(args);
   push_int(0);
}

/****** try_feed - internal main ********************/

/* ---------------------------------------- */
/* helper function to figure out what to do */

static INLINE void recheck_scan(struct parser_html_storage *this,
				int *scan_entity,
				int *scan_tag)
{
   if (this->callback__tag.type!=T_INT) 
      *scan_tag=1;
   else 
      *scan_tag=0;

   if (this->callback__entity.type!=T_INT) 
      *scan_entity=1;
   else 
      *scan_entity=0;
}

/* -------------- */
/* feed to output */

static void put_out_feed(struct parser_html_storage *this,
			 struct pike_string *s)
{
   struct piece *f;

   if (!s->len) return; /* no */

   f=malloc(sizeof(struct piece));
   if (!f)
      error("Parser.HTML(): out of memory\n");
   copy_shared_string(f->s,s);

   f->next=NULL;

   if (this->out_end==NULL)
     this->out=this->out_end=f;
   else
   {
      this->out_end->next=f;
      this->out_end=f;
   }
}

/* ---------------------------- */
/* feed range in feed to output */

static void put_out_feed_range(struct parser_html_storage *this,
			       struct piece *head,
			       int c_head,
			       struct piece *tail,
			       int c_tail)
{
   while (head)
   {
      struct pike_string *ps;
      if (head==tail)
      {
	 ps=string_slice(head->s,c_head,c_tail-c_head);
	 put_out_feed(this,ps);
	 free_string(ps);
	 return;
      }
      ps=string_slice(head->s,c_head,head->s->len-c_head);
      put_out_feed(this,ps);
      free_string(ps);
      c_head=0;
      head=head->next;
   }
   error("internal error: tail not found in feed (put_out_feed_range)\n");
}

/* ------------------------ */
/* push feed range on stack */

static void push_feed_range(struct piece *head,
			    int c_head,
			    struct piece *tail,
			    int c_tail)
{
   int n=0;
   while (head)
   {
      if (head==tail)
      {
	 ref_push_string(string_slice(head->s,c_head,c_tail-c_head));
	 n++;
	 break;
      }
      ref_push_string(string_slice(head->s,c_head,head->s->len-c_head));
      n++;
      if (n==32)
      {
	 f_add(32);
	 n=1;
      }
      c_head=0;
      head=head->next;
   }
   if (!head)
      error("internal error: tail not found in feed (push_feed_range)\n");
   if (!n)
      ref_push_string(empty_string);
   else
      f_add(n);
}


/* ------------------------------------ */
/* skip feed range and count lines, etc */

/* count lines, etc */
static INLINE void skip_piece_range(struct location *loc,
				    struct piece *p,
				    int start,
				    int stop)
{
   int b=loc->byteno;
   switch (p->s->size_shift)
   {
      case 0:
      {
	 p_wchar0 *s=p->s->str;
	 for (;start<stop;start++)
	 {
	    if (*(s++)=='\n') 
	    {
	       loc->linestart=b;
	       loc->lineno++;
	    }
	    b++;
	 }
      }
      break;
      case 1:
      {
	 p_wchar1 *s=(p_wchar1*)p->s->str;
	 for (;start<stop;start++)
	 {
	    if (*(s++)=='\n') 
	    {
	       loc->linestart=b;
	       loc->lineno++;
	    }
	    b++;
	 }
      }
      break;
      case 2:
      {
	 p_wchar2 *s=(p_wchar2*)p->s->str;
	 for (;start<stop;start++)
	 {
	    if (*(s++)=='\n') 
	    {
	       loc->linestart=b;
	       loc->lineno++;
	    }
	    b++;
	 }
      }
      break;
      default:
	 error("unknown width of string\n");
   }
   loc->byteno=b;
}

static void skip_feed_range(struct feed_stack *st,
			    struct piece **headp,
			    int *c_headp,
			    struct piece *tail,
			    int c_tail)
{
   struct piece *head=*headp;
   int c_head=*c_headp;

   DEBUG((stderr,"skip_feed_range %p:%d -> %p:%d\n",
	  *headp,*c_headp,tail,c_tail));

   while (head)
   {
      if (head==tail && c_tail<tail->s->len)
      {
	 skip_piece_range(&(st->pos),head,c_head,c_tail);
	 *c_headp=c_tail;
	 return;
      }
      skip_piece_range(&(st->pos),head,c_head,head->s->len);
      *headp=head->next;
      free_string(head->s);
      free(head);
      head=*headp;
      c_head=0;
   }
}

/* ------------------------------ */
/* scan forward for certain chars */

static int scan_forward(struct piece *feed,
			int c,
			struct piece **destp,
			int *d_p,
			p_wchar2 *look_for,
			int num_look_for) /* negative = skip those */
{
   int rev=0;

   if (num_look_for<0) num_look_for=-num_look_for,rev=1;

   DEBUG((stderr,"scan_forward %p:%d "
	  "num_look_for=%d %slook_for=%d %d %d %d %d\n",
	  feed,c,
	  num_look_for,rev?"(rev) ":"",
	  (num_look_for>0?look_for[0]:-1),
	  (num_look_for>1?look_for[1]:-1),
	  (num_look_for>2?look_for[2]:-1),
	  (num_look_for>3?look_for[3]:-1),
	  (num_look_for>4?look_for[4]:-1)));

   switch (num_look_for)
   {
      case 0: /* optimize, skip to end */
	 while (feed->next)
	    feed=feed->next;
	 *destp=feed;
	 *d_p=feed->s->len;
	 DEBUG((stderr,"scan_forward end...\n"));
	 return 0; /* not found :-) */
	 
      case 1: 
	 if (!rev)
	    while (feed)
	    {
	       int ce=feed->s->len-c;
	       p_wchar2 f=*look_for;
	       DEBUG((stderr,"loop %p:%d .. %p:%d (%d)\n", 
		      feed,c,feed,feed->s->len,ce)); 
	       switch (feed->s->size_shift)
	       {
		  case 0:
		  {
		     p_wchar0*s=((p_wchar0*)feed->s->str)+c;
		     while (ce--)
			if ((p_wchar2)*(s++)==f)
			{
			   c=feed->s->len-ce;
			   goto found;
			}
		  }
		  break;
		  case 1:
		  {
		     p_wchar1*s=((p_wchar1*)feed->s->str)+c;
		     while (ce--)
			if ((p_wchar2)*(s++)==f)
			{
			   c=feed->s->len-ce;
			   goto found;
			}
		  }
		  break;
		  case 2:
		  {
		     p_wchar2 f=(p_wchar2)*look_for;
		     p_wchar2*s=((p_wchar2*)feed->s->str)+c;
		     while (ce--)
			if (*(s++)==f)
			{
			   c=feed->s->len-ce;
			   goto found;
			}
		  }
		  break;
		  default:
		     error("unknown width of string\n");
	       }
	       if (!feed->next) break;
	       c=0;
	       feed=feed->next;
	       break;
	    }

      default:  /* num_look_for > 1 */
	 while (feed)
	 {
	    int ce=feed->s->len-c;
	    DEBUG((stderr,"loop %p:%d .. %p:%d (%d)\n", 
		   feed,c,feed,feed->s->len,ce)); 
	    switch (feed->s->size_shift)
	    {
	       case 0:
	       {
		  int n;
		  p_wchar0*s=((p_wchar0*)feed->s->str)+c;
		  while (ce--)
		  {
		     for (n=0; n<num_look_for; n++)
			if (((p_wchar2)*s)==look_for[n])
			{
			   c=feed->s->len-ce;
			   if (!rev) goto found; else break;
			}
		     if (rev && n==num_look_for) goto found;
		     s++;
		  }
	       }
	       break;
	       case 1:
	       {
		  int n;
		  p_wchar1*s=((p_wchar1*)feed->s->str)+c;
		  while (ce--)
		  {
		     for (n=0; n<num_look_for; n++)
			if (((p_wchar2)*s)==look_for[n])
			{
			   c=feed->s->len-ce;
			   if (!rev) goto found; else break;
			}
		     if (rev && n==num_look_for) goto found;
		     s++;
		  }
	       }
	       break;
	       case 2:
	       {
		  int n;
		  p_wchar2*s=((p_wchar2*)feed->s->str)+c;
		  while (ce--)
		  {
		     for (n=2; n<num_look_for; n++)
			if (((p_wchar2)*s)==look_for[n])
			{
			   c=feed->s->len-ce;
			   if (!rev) goto found; else break;
			}
		     if (rev && n==num_look_for) goto found;
		     s++;
		  }
	       }
	       break;
	       default:
		  error("unknown width of string\n");
	    }
	    if (!feed->next) break;
	    c=0;
	    feed=feed->next;
	 }
	 break;
   }

   DEBUG((stderr,"scan_forward not found %p:%d\n",feed,feed->s->len));
   *destp=feed;
   *d_p=feed->s->len;
   return 0; /* not found */

found:
   *destp=feed;
   *d_p=c-!rev; 
   DEBUG((stderr,"scan_forward found %p:%d (found %d)\n",
	  *destp,*d_p,destp[0]->s->str[*d_p]));
   return 1;
}

static int scan_for_end_of_tag(struct parser_html_storage *this,
			       struct piece *feed,
			       int c,
			       struct piece **destp,
			       int *d_p,
			       int finished)
{
   p_wchar2 ch;
   int res,i;

   /* maybe these should be cached */

   /* bla bla <tag foo 'bar' "gazonk" > */
   /*          ^                      ^ */
   /*       here now             scan here */

   DEBUG((stderr,"scan for end of tag: %p:%d\n",feed,c));

   for (;;)
   {
      /* scan for start of argument quote or end of tag */

      res=scan_forward(feed,c,destp,d_p,
		       this->look_for_start,this->num_look_for_start);
      if (!res) 
	 if (!finished) 
	 {
	    DEBUG((stderr,"scan for end of tag: wait at %p:%d\n",feed,c));
	    return 0; /* not found - no end of tag, yet */
	 }
	 else
	 {
	    DEBUG((stderr,"scan for end of tag: forced end at %p:%d\n",
		   destp[0],*d_p));
	    return 1; /* end of tag, sure... */
	 }

      ch=index_shared_string(destp[0]->s,*d_p);
      if (ch==this->tag_end)
      {
	 DEBUG((stderr,"scan for end of tag: end at %p:%d\n",destp[0],*d_p));
	 return 1; /* end of tag here */
      }

      /* scan for (possible) end(s) of this argument quote */

      for (i=0; i<this->nargq; i++)
	 if (ch==this->argq_start[i]) break;
      res=scan_forward(*destp,d_p[0]+1,destp,d_p,
		       this->look_for_end[i],this->num_look_for_end[i]);
      if (!res)
	 if (!finished) 
	 {
	    DEBUG((stderr,"scan for end of tag: wait at %p:%d\n",
		   destp[0],*d_p));
	    return 0; /* not found - no end of tag, yet */
	 }
	 else
	 {
	    DEBUG((stderr,"scan for end of tag: forced end at %p:%d\n",
		   feed,c));
	    return 1; /* end of tag, sure... */
	 }

      feed=*destp;
      c=d_p[0]+1;
   }
}

/* ---------------------------------------------------------------- */
/* this is called to get data from callbacks and do the right thing */

static int handle_result(struct parser_html_storage *this,
			 struct feed_stack *st,
			 struct piece **head,
			 int *c_head,
			 struct piece *tail,
			 int c_tail)
{
   struct feed_stack *st2;
   int i;

   /* on sp[-1]:
      string: push it to feed stack 
      int: noop, output range
      array(string): output string */

   switch (sp[-1].type)
   {
      case T_STRING: /* push it to feed stack */
	 
	 /* first skip this in local feed*/
	 skip_feed_range(st,head,c_head,tail,c_tail);

	 DEBUG((stderr,"handle_result: pushing string (len=%d) on feedstack\n",
		sp[-1].u.string->len));

	 st2=malloc(sizeof(struct feed_stack));
	 if (!st2)
	    error("out of memory\n");

	 st2->local_feed=malloc(sizeof(struct piece));
	 if (!st2->local_feed)
	 {
	    free(st2);
	    error("out of memory\n");
	 }
	 copy_shared_string(st2->local_feed->s,sp[-1].u.string);
	 st2->local_feed->next=NULL;
	 pop_stack();
	 st2->ignore_data=0;
	 st2->pos=this->stack->pos;
	 st2->prev=this->stack;
	 st2->c=0;
	 this->stack=st2;
	 THIS->stack_count++;
	 return 2; /* please reread stack head */

      case T_INT:
	 switch (sp[-1].u.integer)
	 {
	    case 0:
	       /* just output range */
	       put_out_feed_range(this,*head,*c_head,tail,c_tail);
	       skip_feed_range(st,head,c_head,tail,c_tail);
	       return 0; /* continue */
	    case 1:
	       /* wait: "incomplete" */
	       skip_feed_range(st,head,c_head,tail,c_tail);
	       return 1; /* continue */
	 }
	 error("Parse.HTML: illegal result from callback: %d, "
	       "not 0 (skip) or 1 (wait)\n",
	       sp[-1].u.integer);

      case T_ARRAY:
	 /* output element(s) */
	 for (i=0; i<sp[-1].u.array->size; i++)
	 {
	    if (sp[-1].u.array->item[i].type!=T_STRING)
	       error("Parse.HTML: illegal result from callback: element in array not string\n");

	    put_out_feed(this,sp[-1].u.array->item[i].u.string);
	 }
	 skip_feed_range(st,head,c_head,tail,c_tail);
	 return 0; /* continue */

      default:
	 error("Parse.HTML: illegal result from callback: not 0, string or array(string)\n");   
   }   
}

void do_callback(struct parser_html_storage *this,
		 struct object *thisobj,
		 struct svalue *callback_function,
		 struct piece *start,
		 int cstart,
		 struct piece *end,
		 int cend)
{
   this->start=start;
   this->cstart=cstart;
   this->end=end;
   this->cend=cend;

   ref_push_object(thisobj);
   if (start)
      push_feed_range(start,cstart,end,cend);
   else 
      ref_push_string(empty_string);;
   apply_svalue(callback_function,2);
}


/* ---------------------------------------------------------------- */

static int do_try_feed(struct parser_html_storage *this,
		       struct object *thisobj,
		       struct feed_stack *st,
		       struct piece **feed,
		       int finished)
{
   p_wchar2 look_for[2 /* entity or tag */];
   p_wchar2 ch;
   int n;
   struct piece *dst;
   int cdst;
   int res;

   int scan_entity,scan_tag;

   recheck_scan(this,&scan_entity,&scan_tag);

   for (;;)
   {
      DEBUG((stderr,"%*d do_try_feed scan loop "
	     "scan_entity=%d scan_tag=%d ignore_data=%d\n",
	     this->stack_count,this->stack_count,
	     scan_entity,scan_tag,st->ignore_data));

#ifdef DEBUG
      if (*feed && feed[0]->s->len < st->c) 
	 fatal("len (%d) < st->c (%d)\n",feed[0]->s->len,st->c);
#endif

      /* do we need to check data? */
      if (!st->ignore_data)
      {
	 DEBUG((stderr,"%*d do_try_feed scan for data %p:%d (len=%d)\n",
		this->stack_count,this->stack_count,
		*feed,st->c,feed[0]?feed[0]->s->len:0));

	 /* we are to get data first */
	 /* look for tag or entity */
	 if (*feed)
	 {
	    n=0;
	    if (scan_entity) look_for[n++]=this->entity_start;
	    if (scan_tag) look_for[n++]=this->tag_start;
	    scan_forward(*feed,st->c,&dst,&cdst,look_for,n);
	 }
      
	 if (this->callback__data.type!=T_INT)
	 {
	    DEBUG((stderr,"%*d calling _data callback %p:%d..%p:%d\n",
		   this->stack_count,this->stack_count,
		   *feed,st->c,dst,cdst));
	    
	    do_callback(this,thisobj,
			&(this->callback__data),
			*feed,st->c,dst,cdst);

	    if ((res=handle_result(this,st,
				   feed,&(st->c),dst,cdst)))
	    {
	       DEBUG((stderr,"%*d do_try_feed return %d %p:%d\n",
		      this->stack_count,this->stack_count,
		      res,*feed,st->c));
	       st->ignore_data=(res==1);
	       return res;
	    }
	    recheck_scan(this,&scan_entity,&scan_tag);
	 }
	 else
	 {
	    if (*feed)
	    {
	       put_out_feed_range(this,*feed,st->c,dst,cdst);
	       skip_feed_range(st,feed,&(st->c),dst,cdst);
	    }
	 }

	 DEBUG((stderr,"%*d do_try_feed scan data done %p:%d\n",
		this->stack_count,this->stack_count,
		*feed,st->c));
      }
      /* at end, entity or tag */

      if (!*feed)
      {
	 DEBUG((stderr,"%*d do_try_feed end\n",
		this->stack_count,this->stack_count));
	 return 0; /* done */
      }

      ch=index_shared_string(feed[0]->s,st->c);
      if (scan_entity && ch==this->entity_start) /* entity */
      {
	 DEBUG((stderr,"%*d do_try_feed scan entity %p:%d\n",
		this->stack_count,this->stack_count,
		*feed,st->c));
	 /* just search for end of entity */
	 
	 look_for[0]=this->entity_end;
	 res=scan_forward(*feed,st->c+1,&dst,&cdst,look_for,1);
	 if (!res && !finished) 
	 {
	    st->ignore_data=1; /* no data first at next call */
	    return 1; /* no end, call again */
	 }

	 if (this->callback__entity.type!=T_INT)
	 {
	    DEBUG((stderr,"%*d calling _entity callback %p:%d..%p:%d\n",
		   this->stack_count,this->stack_count,
		   *feed,st->c+1,dst,cdst));

	    /* low-level entity call */
	    do_callback(this,thisobj,
			&(this->callback__entity),
			*feed,st->c+1,dst,cdst);

	    if ((res=handle_result(this,st,
				   feed,&(st->c),dst,cdst+1)))
	    {
	       DEBUG((stderr,"%*d do_try_feed return %d %p:%d\n",
		      this->stack_count,this->stack_count,
		      res,*feed,st->c));
	       st->ignore_data=(res==1);
	       return res;
	    }
	    recheck_scan(this,&scan_entity,&scan_tag);
	 }
	 else
	 {
	    put_out_feed_range(this,*feed,st->c,dst,cdst+1);
	    skip_feed_range(st,feed,&(st->c),dst,cdst+1);
	 }

	 DEBUG((stderr,"%*d do_try_feed scan entity %p:%d\n",
		this->stack_count,this->stack_count,
		*feed,st->c));
      }
      else if (scan_tag && ch==this->tag_start) /* tag */
      {
	 DEBUG((stderr,"%*d do_try_feed scan tag %p:%d\n",
		this->stack_count,this->stack_count,
		*feed,st->c));

	 if (this->callback__tag.type!=T_INT)
	 {
	    res=scan_for_end_of_tag(this,*feed,st->c+1,&dst,&cdst,
				    finished);
	    if (!res) 
	    {
	       st->ignore_data=1;
	       return 1; /* come again */
	    }

	    DEBUG((stderr,"%*d calling _tag callback %p:%d..%p:%d\n",
		   this->stack_count,this->stack_count,
		   *feed,st->c+1,dst,cdst));

	    /* low-level tag call */
	    do_callback(this,thisobj,
			&(this->callback__tag),
			*feed,st->c+1,dst,cdst);

	    st->ignore_data=1;

	    if ((res=handle_result(this,st,
				   feed,&(st->c),dst,cdst+1)))
	    {
	       DEBUG((stderr,"%*d do_try_feed return %d %p:%d\n",
		      this->stack_count,this->stack_count,
		      res,*feed,st->c));
	       st->ignore_data=(res==1);
	       return res;
	    }
	    recheck_scan(this,&scan_entity,&scan_tag);
	 }
	 else
	 {
	    res=scan_for_end_of_tag(this,*feed,st->c+1,&dst,&cdst,
				    finished);
	    if (!res) return 1; /* come again */

	    put_out_feed_range(this,*feed,st->c,dst,cdst+1);
	    skip_feed_range(st,feed,&(st->c),dst,cdst+1);
	 }
      }
      st->ignore_data=0;
   }
}

static void try_feed(int finished)
{
   /* 
      o if tag_stack:
          o pop & parse that
          o ev put on stack
   */

   struct feed_stack *st;

   for (;;)
   {
      switch (do_try_feed(THIS,THISOBJ,
			  THIS->stack,
			  THIS->stack->prev
			  ?&(THIS->stack->local_feed)
			  :&(THIS->feed),
			  finished||(THIS->stack->prev!=NULL)))
      {
	 case 0: /* done, pop stack */
	    if (!THIS->feed) THIS->feed_end=NULL;

	    st=THIS->stack->prev;
	    if (!st) 
	    {
	       THIS->stack->c=0;
	       return; /* all done, but keep last stack elem */
	    }

	    if (THIS->stack->local_feed)
	       error("internal wierdness in Parser.HTML: feed left\n");

	    free(THIS->stack);
	    THIS->stack=st;
	    THIS->stack_count--;
	    break;

	 case 1: /* incomplete, call again */
	    return;

	 case 2: /* reread stack head */
	    if (THIS->stack_count>THIS->max_stack_depth)
	       error("Parse.HTML: too deep recursion\n");
	    break;
      }
   }
}

/****** feed ****************************************/

static void html_feed(INT32 args)
{
   struct piece *f;

   DEBUG((stderr,"feed %d chars\n",
	  (args&&sp[-args].type==T_STRING)?
	  sp[-args].u.string->len:-1));

   if (args)
   {
      if (sp[-args].type!=T_STRING)
	 error("feed: illegal arguments\n");

      f=malloc(sizeof(struct piece));
      if (!f)
	 error("feed: out of memory\n");
      copy_shared_string(f->s,sp[-args].u.string);

      f->next=NULL;

      if (THIS->feed_end==NULL)
      {
	 DEBUG((stderr,"  (new feed)\n"));
	 THIS->feed=THIS->feed_end=f;
      }
      else
      {
	 DEBUG((stderr,"  (attached to feed end)\n"));
	 THIS->feed_end->next=f;
	 THIS->feed_end=f;
      }
   }

   if (args<2 || sp[1-args].type!=T_INT || sp[1-args].u.integer)
   {
      pop_n_elems(args);
      try_feed(0);
   }
   else
      pop_n_elems(args);

   ref_push_object(THISOBJ);
}

static void html_feed_insert(INT32 args)
{
   struct feed_stack *st2;

   if (!args ||
       sp[-args].type!=T_STRING)
      error("feed_insert: illegal arguments\n");

   DEBUG((stderr,"html_feed_insert: "
	  "pushing string (len=%d) on feedstack\n",
	  sp[-args].u.string->len));

   st2=malloc(sizeof(struct feed_stack));
   if (!st2)
      error("out of memory\n");

   st2->local_feed=malloc(sizeof(struct piece));
   if (!st2->local_feed)
   {
      free(st2);
      error("out of memory\n");
   }
   copy_shared_string(st2->local_feed->s,sp[-args].u.string);
   st2->local_feed->next=NULL;
   pop_stack();
   st2->ignore_data=0;
   st2->pos=THIS->stack->pos;
   st2->prev=THIS->stack;
   st2->c=0;
   THIS->stack=st2;
   THIS->stack_count++;

   if (args<2 || sp[1-args].type!=T_INT || sp[1-args].u.integer)
   {
      pop_n_elems(args);
      try_feed(0);
   }
   else
      pop_n_elems(args);
   ref_push_object(THISOBJ);
}

static void html_finish(INT32 args)
{
   pop_n_elems(args);
   if (THIS->feed || THIS->stack->prev) try_feed(1);
   ref_push_object(THISOBJ);
}

static void html_read(INT32 args)
{
   int n;
   int m=0; /* strings on stack */

   if (!args) 
      n=0x7fffffff; /* a lot */
   else  if (sp[-args].type==T_INT)
      n=sp[-args].u.integer;
   else
      error("read: illegal argument\n");

   pop_n_elems(args);

   /* collect up to n characters */

   while (THIS->out && n)
   {
      struct piece *z;

      if (THIS->out->s->len>n)
      {
	 struct pike_string *ps;
	 ref_push_string(string_slice(THIS->out->s,0,n));
	 m++;
	 ps=string_slice(THIS->out->s,n,THIS->out->s->len-n);
	 free_string(THIS->out->s);
	 THIS->out->s=ps;
	 break;
      }
      n-=THIS->out->s->len;
      ref_push_string(THIS->out->s);
      m++;
      if (m==32)
      {
	 f_add(32);
	 m=1;
      }
      z=THIS->out;
      THIS->out=THIS->out->next;
      free(z);
   }

   if (!THIS->out) 
      THIS->out_end=NULL;

   if (!m) 
      ref_push_string(empty_string);
   else
      f_add(m);
}

void html_write_out(INT32 args)
{
   if (!args||sp[-args].type!=T_STRING)
      error("write_out: illegal arguments\n");
   
   put_out_feed(THIS,sp[-1].u.string);
   pop_n_elems(args);
   ref_push_object(THISOBJ);
}

/** query *******************************************/

/*
**! method string current()
**!	Gives the current range of data, ie the contents
**!	of the tag/entity/etc for the current callback.
*/

static void html_current(INT32 args)
{
   pop_n_elems(args);
   if (THIS->start)
   {
      push_feed_range(THIS->start,THIS->cstart,THIS->end,THIS->cend);
   }
   else
      ref_push_string(empty_string);
}

/*
**! method array tag()
**! method string tag_name()
**! method string tag_args()
**! method array tag(mixed default_value)
**! method string tag_args(mixed default_value)
**!     This gives parsed information about the 
**!	current tag. 
**!
**!	<tt>tag_name</tt> gives the name of the current tag,
**!
**!	<tt>tag_args</tt> is the arguments of the current tag,
**!	parsed to a convinient mapping consisting of key:value pairs. 
**!	The default value (if no default_value is given) is the same 
**!	string as the key. 
**!
**!	<tt>tag()</tt> gives the equivalent of
**!	<tt>({tag_name(),tag_args()})</tt>.
*/

static void html_tag_name(INT32 args)
{
   struct piece *s1=NULL,*s2=NULL;
   int c1=0,c2=0;
   int n;

   /* get rid of arguments */
   pop_n_elems(args);

   /* scan start of tag name */
   scan_forward(THIS->start,THIS->cstart,&s1,&c1,
		whitespace,-(int)(sizeof(whitespace)/sizeof(whitespace[0])));
   /* scan end of tag name */
   scan_forward(s1,c1,&s2,&c2,
		ws_or_endarg,sizeof(ws_or_endarg)/sizeof(ws_or_endarg[0]));
   
   DEBUG((stderr,"tag_name %p:%d .. %p:%d\n",s1,c1,s2,c2));

   push_feed_range(s1,c1,s2,c2);
   
   n=0;
}

static void html_tag_args(INT32 args)
{
   pop_n_elems(args);
   f_aggregate_mapping(0);
}

/** debug *******************************************/

/*
**! method mapping _inspect()
**! 	This is a low-level way of debugging a parser.
**!	This gives a mapping of the internal state
**!	of the Parser.HTML object.
**!
**!	The format and contents of this mapping may
**!	change without further notice.
*/

void html__inspect(INT32 args)
{
   int n=0,m,o,p;
   struct piece *f;
   struct feed_stack *st;

   pop_n_elems(args);
   
   push_text("feed");
   m=0;
   
   st=THIS->stack;
   while (st)
   {
      o=0;
      p=0;

      push_text("feed");

      if (st->local_feed)
	 f=st->local_feed;
      else 
	 f=THIS->feed;

      while (f)
      {
	 ref_push_string(f->s);
	 p++;
	 f=f->next;
      }

      f_aggregate(p);
      o++;

      push_text("position");
      push_int(st->c);
      o++;
      
      push_text("byteno");
      push_int(st->pos.byteno);
      o++;

      push_text("lineno");
      push_int(st->pos.lineno);
      o++;

      push_text("linestart");
      push_int(st->pos.linestart);
      o++;

      f_aggregate_mapping(o*2);
      st=st->prev;

      m++;
   }

   f_aggregate(m);
   n++;

   push_text("outfeed");
   p=0;
   f=THIS->out;
   while (f)
   {
      ref_push_string(f->s);
      p++;
      f=f->next;
   }
   f_aggregate(p);
   n++;

   push_text("callback__tag");
   push_svalue(&(THIS->callback__tag));
   n++;

   push_text("callback__entity");
   push_svalue(&(THIS->callback__entity));
   n++;

   push_text("callback__data");
   push_svalue(&(THIS->callback__data));
   n++;

   f_aggregate_mapping(n*2);
}

/** calculate ***************************************/

void html_parse_get_tag(INT32 args)
{
   struct piece feed;
   check_all_args("parse_get_tag",args,BIT_STRING,0);
   feed.s=sp[-args].u.string->str;
   feed.next=NULL;
   parse_get_tag(feed);
   stack_pop_n_elems_keep_top(args);
}

/****** module init *********************************/

void init_parser_html(void)
{
   MAKE_CONSTANT_SHARED_STRING(empty_string,"");

   ADD_STORAGE(struct parser_html_storage);

   set_init_callback(init_html_struct);
   set_exit_callback(exit_html_struct);

#define CBRET "string|array(string)" /* 0|string|({string}) */

   /* feed control */

   add_function("feed",html_feed,
		"function(:object)|"
		"function(string,void|int:object)",0);
   add_function("finish",html_finish,
		"function(:object)",0);
   add_function("read",html_read,
		"function(void|int:string)",0);

   add_function("write_out",html_write_out,
		"function(string:object)",0);
   add_function("feed_insert",html_feed_insert,
		"function(string,void|int:object)",0);

   /* query */

   add_function("current",html_current,
		"function(:string)",0);

   add_function("tag_name",html_tag_name,
		"function(:string)",0);

   add_function("tag_args",html_tag_args,
		"function(:mapping)",0);

   /* special callbacks */

   add_function("_set_tag_callback",html__set_tag_callback,
		"function(function(object,string,mixed ...:"CBRET"):void)",0);
   add_function("_set_data_callback",html__set_data_callback,
		"function(function(object,string,mixed ...:"CBRET"):void)",0);
   add_function("_set_entity_callback",html__set_entity_callback,
		"function(function(object,string,mixed ...:"CBRET"):void)",0);

   /* debug, whatever */
   
   add_function("_inspect",html__inspect,
		"function(:mapping)",0);
}


/*

class Parse_HTML
{
   void feed(string something); // more data in stream
   void finish(); // stream ends here

   string read(void|int chars); // read out-feed

   void reset();  // reset stream

   object clone(); // new object, fresh stream

   // argument quote ( < ... foo="bar" foo='bar' ...> )
   void set_quote(string start,string end);
   void set_quote(string start,string end,
                  string start2,string end2, ...);      // tupels

   // tag quote
   void set_tag_quote(string start,string end); // "<" ">"

   // call to_call(this,mapping(string:string) args,...extra)
   void add_tag(string tag,function to_call);

   // call to_call(this,mapping(string:string) args,string cont,...extra)
   void add_container(string tag,function to_call);

   // same as above, but tries globs (slower<tm>)
   void add_glob_tag(string tag,function to_call);
   void add_glob_container(string tag,function to_call);

   // set extra args
   void extra(mixed ...extra);

   // query where we are now
   string current();  // current tag/entity/etc data string 

   array tag();       // tag data: ({string name,mapping arguments})
   string tag_args(); // tag arguments only
   string tag_name(); // tag name only

   array at();        // current position: ({line,char,column})
   int at_line();     // line number (first=1)
   int at_char();     // char (first=1)
   int at_column();   // column (first=1)

   // low-level callbacks
   // calls to_call(this,string data)
   void _set_tag_callback(function to_call);
   void _set_data_callback(function to_call);
   void _set_entity_callback(function to_call);

   // just useful
   mapping parse_get_tag(string tag);
   mapping parse_get_args(string tag);

   // entity quote
   void set_entity_quote(string start,string end); // "&",";"

   int set_allow_open_entity(void|int yes);        // &entfoo<bar> -> ent

   // call to_call(this,string entity,...extra);
   void add_entity(string entity,function to_call);
   void add_glob_entity(string entity,function to_call);
}

*/
