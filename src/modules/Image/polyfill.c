#include "global.h"
RCSID("$Id: polyfill.c,v 1.27 1999/04/13 12:32:33 mirar Exp $");

/* Prototypes are needed for these */
extern double floor(double);

#include <unistd.h>
#include <math.h>

#include "config.h"

#include "stralloc.h"
#include "pike_macros.h"
#include "object.h"
#include "constants.h"
#include "interpret.h"
#include "svalue.h"
#include "array.h"
#include "threads.h"
#include "builtin_functions.h"

#include "image.h"

#ifdef THIS
#undef THIS
#endif
#define THIS ((struct image *)(fp->current_storage))
#define THISOBJ (fp->current_object)

#undef POLYDEBUG

/*
**! module Image
**! note
**!	$Id: polyfill.c,v 1.27 1999/04/13 12:32:33 mirar Exp $
**! class Image
*/

/*
**! method object polyfill(array(int|float) ... curve)
**! 	fills an area with the current color
**!
**! returns the current object
**! arg array(int|float) curve
**! 	curve(s), <tt>({x1,y1,x2,y2,...,xn,yn})</tt>,
**! 	automatically closed.
**!
**!	If any given curve is inside another, it
**!	will make a hole.
**!
**! note
**!	Lines in the polygon may <i>not</i> be crossed without
**!	the crossing coordinate specified in both lines.
**!
**! bugs
**!	Inverted lines reported on Intel and Alpha processors.
**!
**! see also: setcolor
*/

struct line_list
{
   struct vertex *above,*below;
   float dx,dy;
   struct line_list *next;
   float xmin,xmax,yxmin,yxmax; /* temporary storage */
};

struct vertex
{
   float x,y;
   struct vertex *next;    /* total list, sorted downwards */
   struct line_list *below,*above; /* childs */
   int done;
};

#define VY(V,X) ((V)->above->y+(V)->dy*((X)-(V)->above->x))

static struct vertex *vertex_new(float x,float y,struct vertex **top)
{
   struct vertex *c;
   while (*top && (*top)->y<y) top=&((*top)->next);
   
   if (*top && 
       (*top)->x==x && (*top)->y==y) return *top; /* found one */

   c=malloc(sizeof(struct vertex));
   if (!c) return NULL;
   c->x=x;
   c->y=y;
   c->next=*top;
   c->above=c->below=NULL;
   c->done=0;
   *top=c;

   return c;
}

static void vertex_connect(struct vertex *above,
			   struct vertex *below)
{
   struct line_list *c,*d;
   float diff;

   if (below==above) return;

   c = malloc(sizeof(struct line_list));
   if (!c) return;
   c->above = above; c->below = below;
   c->next = above->below;
   if (((diff = (below->y - above->y)) < 1.0e-10) &&
       (diff > -1.0e-10))
     c->dx = 1.0e10;
   else
     c->dx = (below->x - above->x)/diff;
   if (((diff = (below->x - above->x)) < 1.0e-10) &&
       (diff > -1.0e-10))
     c->dy = 1.0e10;
   else
     c->dy = (below->y - above->y)/diff;
   above->below = c;

   d = malloc(sizeof(struct line_list));
   d->above = above; d->below = below;
   d->next = below->above;
   d->dx = c->dx;
   d->dy = c->dy;
   below->above = d;
}

static INLINE float line_xmax(struct line_list *v,float yp,float *ydest)
{
   if (v->dx>0.0) 
      if (v->below->y>yp+1.0+1e-10)
	 return v->above->x+v->dx*((*ydest=(yp+1.0))-v->above->y);
      else
	 return (*ydest=v->below->y),v->below->x;
   else if (v->dx<0.0) 
      if (v->above->y<yp-1e-10)
	 return v->above->x+v->dx*((*ydest=yp)-v->above->y);
      else
	 return (*ydest=v->above->y),v->above->x;
   else if (v->below->y>yp+1.0+1e-10) 
      return (*ydest=yp+1.0),v->above->x;
   else
      return (*ydest=v->below->y),v->below->x;
}

static INLINE float line_xmin(struct line_list *v,float yp,float *ydest)
{
   if (v->dx<0.0) 
      if (v->below->y>yp+1.0+1e-10)
	 return v->above->x+v->dx*((*ydest=(yp+1.0))-v->above->y);
      else
	 return (*ydest=v->below->y),v->below->x;
   else if (v->dx>0.0) 
      if (v->above->y<yp-1e-10)
	 return v->above->x+v->dx*((*ydest=yp)-v->above->y);
      else
	 return (*ydest=v->above->y),v->above->x;
   else if (v->above->y<yp-1e-10) 
      return (*ydest=yp),v->above->x;
   else
      return (*ydest=v->above->y),v->above->x;
}

static void add_vertices(struct line_list **first,
			 struct line_list *what,
			 float yp)
{
   struct line_list **ins,*c;
#ifdef POLYDEBUG
   char *why="unknown";
#define BECAUSE(X) (why=(X))
#else
#define BECAUSE(X)
#endif

   while (what)
   {
      what->xmin=line_xmin(what,yp,&what->yxmin);
      what->xmax=line_xmax(what,yp,&what->yxmax);

      ins=first;

#ifdef POLYDEBUG
   fprintf(stderr,"  insert %g,%g - %g,%g [%g,%g - %g,%g]\n",
	  what->xmin,what->yxmin,
	  what->xmax,what->yxmax,
	  what->above->x,what->above->y,
	  what->below->x,what->below->y);
#endif

      /* fast jump vectorgroups on the left */
      while (*ins)
      {
	 if ((*ins)->xmax>what->xmin) break;
	 ins=&((*ins)->next);
      }

      /* ok, place in correct y for x=what->xmin or x=(*ins)->xmin */

      while (*ins)
      {
	 /* case: -what-> <-ins- */
	 BECAUSE("what is left of ins");
	 if ((*ins)->xmin>=what->xmax)  
	    break; /* place left of */

	 /* case: -what-    */
	 /*       <-ins-    */

	 if ((*ins)->xmin==what->xmin &&
	     (*ins)->yxmin==what->yxmin)
	 {
	    BECAUSE("ins is above (left exact) what");
	    if (VY((*ins),what->xmax)>what->yxmax) break;
	    else { ins=&((*ins)->next); continue; }
	 }

	 /* case: -what-    */
	 /*       -ins->    */

	 if ((*ins)->xmax==what->xmax &&
	     (*ins)->yxmax==what->yxmax)
	 {
	    BECAUSE("ins is above (right exact) what");
	    if (VY((*ins),what->xmin)>what->yxmin) break;
	    else { ins=&((*ins)->next); continue; }
	 }

	 /* case: -what-    */
	 /*          <-ins- */
	 if ((*ins)->xmin>what->xmin)
	 { 
	    BECAUSE("ins is below (right) what");
	    if ((*ins)->yxmin>VY(what,(*ins)->xmin)) break; 
	 }
	 /* case:   <-what-    */
	 /*       -ins-        */
	 else 
	 {
	    BECAUSE("what is above ins (left)");
	    if (VY((*ins),what->xmin)>what->yxmin) break; 
	 }

	 /* case: -what->    */
	 /*           -ins-  */
	 if ((*ins)->xmax>what->xmax)
	 { 
	    BECAUSE("what is above ins (right)");
	    if (VY((*ins),what->xmax)>what->yxmax) break; 
	 }
	 /* case:    -what-    */
	 /*       -ins->       */
	 else 
	 {
	    BECAUSE("ins is below (left) what");
	    if ((*ins)->yxmax>VY(what,(*ins)->xmax)) break; 
	 }

	 ins=&((*ins)->next);
      }


#ifdef POLYDEBUG
      if (*ins)
	 fprintf(stderr,"     before %g,%g - %g,%g [%g,%g - %g,%g] because %s\n",
		 (*ins)->xmin,(*ins)->yxmin,
		 (*ins)->xmax,(*ins)->yxmax,
		 (*ins)->above->x,(*ins)->above->y,
		 (*ins)->below->x,(*ins)->below->y,
		 why);
#endif


      c=malloc(sizeof(struct line_list));
      *c=*what;
      c->next=*ins;
      *ins=c;

      what=what->next;
   }
}

static void sub_vertices(struct line_list **first,
			 struct vertex *below,
			 float yp)
{
   struct line_list **ins,*c;

   ins=first;

#ifdef POLYDEBUG
   fprintf(stderr,"  remove %lx <-%g,%g\n",below,below->x,below->y);
#endif

   while (*ins)
   {
      if ((*ins)->below==below) 
      {
#ifdef POLYDEBUG
	 fprintf(stderr,"   removing %lx,[%g,%g-%g,%g] %lx,%lx \n",
		 *ins,
		 (*ins)->above->x,(*ins)->above->y,
		 (*ins)->below->x,(*ins)->below->y,
		 (*ins)->above,(*ins)->below);
#endif
	 c=*ins;
	 *ins=(*ins)->next;
	 free(c);
      }
      else 
      { 
	 ins=&((*ins)->next);
      }
   }
}

static INLINE void polyfill_row_add(float *buf,
				    float xmin,float xmax,
				    float add)
{
   int i;
   int xmin_i = (int)floor(xmin);
   int xmax_i = (int)floor(xmax);
   if (xmax_i<0) return;
   if (xmin_i == xmax_i)
      buf[xmin_i] += (xmax-xmin)*add;
   else if (xmin_i>=0)
   {
      buf[xmin_i] += (1-(xmin-((float)xmin_i)))*add;
      for (i=xmin_i+1; i<xmax_i; i++) buf[i]+=add;
      buf[xmax_i] += add*(xmax-((float)xmax_i));
   }
   else
   {
      for (i=0; i<xmax_i; i++) buf[i]+=add;
      buf[xmax_i] += add*(xmax-((float)xmax_i));
   }
}

static INLINE void polyfill_slant_add(float *buf,
				      float xmin,float xmax,
				      float lot,
				      float y1,
				      float dy)
{
   int i;
   int xmin_i = (int)floor(xmin);
   int xmax_i = (int)floor(xmax);
   if (xmax_i<0) return;
   if (xmin_i == xmax_i)
      buf[xmin_i] += (xmax-xmin)*(y1+dy*(xmax-xmin)/2)*lot;
   else if (xmin_i>=0)
   {
      buf[xmin_i] += 
	 lot*(y1+dy*(1-(xmin-((float)xmin_i)))/2)*(1-(xmin-((float)xmin_i)));
      y1+=dy*(1-(xmin-((float)xmin_i)));
      for (i=xmin_i+1; i<xmax_i; i++) 
	 buf[i]+=lot*(y1+dy/2),y1+=dy;
      buf[xmax_i] += 
	 lot*(y1+dy*(xmax-((float)xmax_i))/2)*(xmax-((float)xmax_i));
   }
   else
   {
      y1+=dy*(0.0-xmin);
      for (i=0; i<xmax_i; i++) 
	 buf[i]+=lot*(y1+dy/2),y1+=dy;
      buf[xmax_i] += 
	 lot*(y1+dy*(xmax-((float)xmax_i))/2)*(xmax-((float)xmax_i));
   }
}

static int polyfill_event(float xmin,
			  float xmax,
			  struct line_list **pll,
			  int tog,
			  float yp,
			  float *buf)
{
   struct line_list *c;
   struct line_list *ll=*pll;
   int mtog;

#ifdef POLYDEBUG
   fprintf(stderr," event %g,%g - %g,%g tog=%d\n",xmin,yp,xmax,yp+1.0,tog);
#endif

   /* toggle for lines ended at xmin,yp */
   c=ll;
   while (c)
   {
      if ( (c->above->y < yp) &&
	   ( (c->xmax==xmin && c->yxmax==yp) ||
	     (c->xmin==xmin && c->yxmin==yp) ) )
      {
#ifdef POLYDEBUG
	 fprintf(stderr,"    toggle for %g,%g - %g,%g [%g,%g - %g,%g]\n",
		 c->xmin,c->yxmin,c->xmax,c->yxmax,
		 c->above->x,c->above->y,c->below->x,c->below->y);
#endif
	 tog=!tog;
      }
      c=c->next;
   }

#if 0
   /* sanity check */
   c=ll;
   while (c && c->next)
   {
      if (c->xmin > c->next->xmax ||
	  ( c->xmin!=c->xmax &&
	    c->next->xmin!=c->next->xmax &&
	    c->xmax>=xmin &&
	    c->xmin<=xmin &&
	    c->next->xmax>=xmin &&
	    c->next->xmin<=xmin &&
	    (VY(c,xmin)>VY(c->next,xmin) ||
	     VY(c,xmax)>VY(c->next,xmax))) )
      {
	 struct line_list *l1;
	 /* resort */
#ifdef POLYDEBUG
	 fprintf(stderr,"  !!! internal resort !!!\n");
	 fprintf(stderr,"  on pair: %g,%g - %g,%g [%g,%g - %g,%g]\n           %g,%g - %g,%g [%g,%g - %g,%g]\n",
		 c->xmin,c->yxmin,c->xmax,c->yxmax,
		 c->above->x,c->above->y,c->below->x,c->below->y,
		 c->next->xmin,c->next->yxmin,c->next->xmax,c->next->yxmax,
		 c->next->above->x,c->next->above->y,
		 c->next->below->x,c->next->below->y);
#endif
	 l1=NULL;
	 add_vertices(&l1,ll,yp);

	 while ((c=*pll))
	 {
	    *pll=c->next;
	    free(c);
	 }

	 ll=*pll=l1;

	 break;
      }
      c=c->next;
   }
#endif

   /* paint if needed */
   if (tog)
   {
#ifdef POLYDEBUG
	 fprintf(stderr,"  fill %g..%g with 1.0\n",xmin,xmax);
#endif	 
      polyfill_row_add(buf,xmin,xmax,1.0);
   }

   /* loop over events */
   mtog=tog;
   c=ll;

   while (c)
   {
      if (c->xmin<=xmin && c->xmax>=xmax)
      {
	 float y1=VY(c,xmin);
#ifdef POLYDEBUG
	 float y2=VY(c,xmax);
	 fprintf(stderr,"  use line %g,%g - %g,%g [%g,%g - %g,%g] : %g,%g - %g,%g = %+g\n",
		 c->xmin,c->yxmin,c->xmax,c->yxmax,
		 c->above->x,c->above->y,c->below->x,c->below->y,
		 xmin,y1,xmax,y2,(tog?-1.0:1.0)*((y1+y2)/2.0-yp));
#endif	 
	 polyfill_slant_add(buf,xmin,xmax,(tog?-1.0:1.0),(yp+1)-y1,-c->dy);
	 tog=!tog;
      }
      if (c->xmin>xmax) break; /* skip the rest */
      c=c->next;
   }
   

   return mtog;
}

static void polyfill_some(struct image *img,
			  struct vertex *v,
			  float *buf)
{
   struct line_list *ll=NULL;
   int y=0;
   float ixmax=(float)img->xsize;
   struct vertex *to_add=v,*to_loose=v;
   /* beat row for row */
   
   if (y+1.0+1e-10<v->y) 
      y=(int)v->y;

   while (y<img->ysize && (to_loose||to_add) )
   {
      float yp=y;
      struct line_list *c;
      float xmin,xmax;
      int tog=0;
      int i;
      rgb_group *d;

#ifdef POLYDEBUG      
      fprintf(stderr,"\nline %d..%d\n",y,y+1);
#endif

      /* update values for current lines */
      c=ll;
      while (c)
      {
	 c->xmin=line_xmin(c,yp,&c->yxmin);
	 c->xmax=line_xmax(c,yp,&c->yxmax);
	 c=c->next;
      }

      /* add any new vertices */
      while (to_add && to_add->y<yp+1.0)
      {
	 struct vertex *vx=to_add;
	 to_add=to_add->next;
	 add_vertices(&ll,vx->below,yp);
      }

#ifdef POLYDEBUG      
      c=ll;
      while (c)
      {
	 fprintf(stderr,"  line %g,%g - %g,%g [%g,%g - %g,%g]\n",
		 c->xmin,c->yxmin,c->xmax,c->yxmax,
		 c->above->x,c->above->y,c->below->x,c->below->y);
	 c=c->next;
      }
      
#endif

      if (!ll)
      {
	 y++;
	 continue;
      }

      /* begin with zeros */
      for (i=0; i<img->xsize; i++) buf[i]=0.0;

      /* sanity check */
      c=ll;
      while (c && c->next)
      {
	 if (c->xmin > c->next->xmax ||
	     c->xmax > c->next->xmin ||
	     ( c->xmin!=c->xmax &&
	       c->next->xmin!=c->next->xmax &&
	       c->next->xmax>=c->xmin &&
	       c->next->xmin<=c->xmin &&
	       VY(c,c->xmin)>VY(c->next,c->xmin)))
	 {
	    struct line_list *l1;
	    struct line_list *pll;
	    /* resort */
#ifdef POLYDEBUG
	    fprintf(stderr,"  !!! resort !!!\n");
#endif
	    l1=NULL;
	    add_vertices(&l1,ll,yp);

	    while ((c=ll))
	    {
	       ll=c->next;
	       free(c);
	    }

	    ll=l1;

	    break;
	 }
	 c=c->next;
      }

      /* find first horisintal event */
      xmin=ll->xmin;
      c=ll;
      while (c)
      {
	 if (c->xmin<xmin) xmin=c->xmin;
	 c=c->next;
      }

      /* loop through all horisontal events */
      while (xmin<ixmax)
      {
	 xmax=1e10;
	 c=ll;
	 while (c)
	 {
	    /* each line has two events: beginning and end */
	    if (c->xmin<xmax && c->xmin>xmin) xmax=c->xmin;
	    if (c->xmax<xmax && c->xmax>xmin) xmax=c->xmax;
	    c=c->next;
	 }
	 if (xmax==1e10) break; /* no more events */

	 if (xmax>ixmax) xmax=ixmax;
	 tog=polyfill_event(xmin,xmax,&ll,tog,yp,buf);
	 
	 /* shift to get next event */
	 xmin=xmax;
	 xmax=xmin-1.0;
      }
      
      
      /* remove any old vertices */
      while (to_loose!=to_add && to_loose->y<yp+1.0-1e-10)
      {
	 struct vertex *vx=to_loose;
	 to_loose=to_loose->next;
	 sub_vertices(&ll,vx,yp);
      }

      /* write this row */
      d=img->img+img->xsize*y;
      for (i=0; i<img->xsize; i++)
      {
#ifdef POLYDEBUG
	 fprintf(stderr,"%3.2f ",buf[i]);
#endif
	 d->r=(COLORTYPE)((d->r*(1.0-buf[i]))+(img->rgb.r*buf[i]));
	 d->g=(COLORTYPE)((d->g*(1.0-buf[i]))+(img->rgb.g*buf[i]));
	 d->b=(COLORTYPE)((d->b*(1.0-buf[i]))+(img->rgb.b*buf[i]));
	 d++;
      }
#ifdef POLYDEBUG
      fprintf(stderr,"\n");
#endif

      y++;
   }
   while (ll)
   {
      struct line_list *c;
      ll=(c=ll)->next;
      free(c);
   }
}

static INLINE void polyfill_free(struct vertex *top)
{
   struct line_list *v,*vn;
   struct vertex *tn;

   while (top)
   {
      v=top->above;
      while (v) { vn=v->next; free(v); v=vn; }
      v=top->below;
      while (v) { vn=v->next; free(v); v=vn; }
      tn=top->next;
      free(top);
      top=tn;
   }
}

static INLINE struct vertex *polyfill_begin(void)
{
   return NULL;
}

static INLINE struct vertex *polyfill_add(struct vertex *top,
					  struct array *a,
					  int arg,
					  char* what)
{
   struct vertex *first,*last,*cur = NULL;
   int n;

   for (n=0; n<a->size; n++)
      if (a->item[n].type!=T_FLOAT &&
	  a->item[n].type!=T_INT)
      {
	 polyfill_free(top);
	 error("Illegal argument %d to %s, array index %d is not int nor float\n",arg,what,n);
	 return NULL;
      }

   if (a->size<6) 
   {
      return top; 
#if 0
      polyfill_free(top);
      error("Illegal argument %d to %s, too few vertices (min 3)\n", arg, what);
      return NULL; /* no polygon with less then tree corners */
#endif
   }

#define POINT(A,N) (((A)->item[N].type==T_FLOAT)?((A)->item[N].u.float_number):((float)((A)->item[N].u.integer)))

   last=first=vertex_new(POINT(a,0),POINT(a,1),&top);

   if (!last) return NULL;

   for (n=2; n+1<a->size; n+=2)
   {
      cur=vertex_new(POINT(a,n),POINT(a,n+1),&top);
      if (!cur) return NULL;
      if (cur->y<last->y)
	 vertex_connect(cur,last);
      else if (cur->y>last->y)
	 vertex_connect(last,cur);
      else if (cur->x<last->x)
	 vertex_connect(cur,last);
      else
	 vertex_connect(last,cur);

      last=cur;
   }

   if (cur->y<first->y)
      vertex_connect(cur,first);
   else if (cur->y>first->y)
      vertex_connect(first,cur);
   else if (cur->x<first->x)
      vertex_connect(cur,first);
   else
      vertex_connect(first,cur);

   return top;
}

void image_polyfill(INT32 args)
{
   struct vertex *v;
   float *buf;

   if (!THIS->img)
      error("Image.Image->polyfill: no image\n");

   buf=malloc(sizeof(float)*(THIS->xsize+1));
   if (!buf)
      error("Image.Image->polyfill: out of memory\n");

   v=polyfill_begin();

   while (args)
   {
      struct vertex *v_tmp;

      if (sp[-1].type!=T_ARRAY)
      {
	 polyfill_free(v);
	 error("Image.Image->polyfill: Illegal argument %d, expected array\n",
	       args);
      }
      if ((v_tmp=polyfill_add(v, sp[-1].u.array, args, "Image.Image->polyfill()"))) {
	 v = v_tmp;
      } else {
	 polyfill_free(v);
	 error("Image.Image->polyfill: Bad argument %d, bad vertex\n", args);
      }
      args--;
      pop_stack();
   }

   if (!v) return; /* no vertices */

   polyfill_some(THIS,v,buf);
   
   polyfill_free(v);
   
   free(buf);

   ref_push_object(THISOBJ);
}

