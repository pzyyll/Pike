/*
|| This file is part of Pike. For copyright information see COPYRIGHT.
|| Pike is distributed under GPL, LGPL and MPL. See the file COPYING
|| for more information.
*/

#ifndef GLOBAL_H
#define GLOBAL_H

/* Mingw32 workarounds */
#if (defined(__WINNT__) || defined(__WIN32__)) && !defined(__NT__)
#define __NT__
#endif

#ifndef _LARGEFILE_SOURCE
#  define _FILE_OFFSET_BITS 64
#  define _LARGEFILE_SOURCE
/* #  define _LARGEFILE64_SOURCE 1 */	/* This one is for explicit 64bit. */
#endif

/* HPUX needs these too... */
#ifndef __STDC_EXT__
#  define __STDC_EXT__
#endif /* !__STDC_EXT__ */
#ifndef _PROTOTYPES
#  define _PROTOTYPES
#endif /* !_PROTOTYPES */

/* And Linux wants this one... */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif /* !_GNU_SOURCE */

#ifdef __NT__
/* To get <windows.h> to stop including the entire OS,
 * we need to define this one.
 */
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

/* We also need to ensure that we get the WIN32 APIs. */
#ifndef WIN32
#define WIN32	100	/* WinNT 1.0 */
#endif

/* We want WinNT 5.0+ API's if available.
 *
 * We avoid the WinNT 6.0+ API's for now.
 */
#if !defined(_WIN32_WINDOWS) || (_WIN32_WINDOWS < 0x5ff)
#undef _WIN32_WINDOWS
#define _WIN32_WINDOWS 0x05ff
#endif

#if !defined(_WIN32_WINNT) || (_WIN32_WINNT < 0x5ff)
#undef _WIN32_WINNT
#define _WIN32_WINNT 0x05ff
#endif

/* In later versions of the WIN32 SDKs, we also need to define this one. */
#if !defined(NTDDI_VERSION) || (NTDDI_VERSION < 0x05ffffff)
#undef NTDDI_VERSION
#define NTDDI_VERSION 0x05ffffff
#endif

#endif /* __NT__ */

#ifdef __amigaos__
/* Avoid getting definitions of struct in_addr from <unistd.h>... */
#define __USE_NETINET_IN_H
#endif

/*
 * Some structure forward declarations are needed.
 */

/* This is needed for linux */
#ifdef MALLOC_REPLACED
#define NO_FIX_MALLOC
#endif

#ifndef STRUCT_PROGRAM_DECLARED
#define STRUCT_PROGRAM_DECLARED
struct program;
#endif

struct function;
#ifndef STRUCT_SVALUE_DECLARED
#define STRUCT_SVALUE_DECLARED
struct svalue;
#endif
struct sockaddr;
struct object;
struct array;
struct svalue;

#ifndef STRUCT_TIMEVAL_DECLARED
#define STRUCT_TIMEVAL_DECLARED
struct timeval;
#endif

#ifndef CONFIGURE_TEST
/* machine.h doesn't exist if we're included from a configure test
 * program. In that case these defines will already be included. */

/* Newer autoconf adds the PACKAGE_* defines for us, regardless
 * whether we want them or not. If we're being included from a module
 * they will clash, and so we need to ensure the one for the module
 * survives, either they are defined already or get defined later.
 * Tedious work.. */
#ifndef PIKE_CORE
#ifdef PACKAGE_NAME
#define ORIG_PACKAGE_NAME PACKAGE_NAME
#undef PACKAGE_NAME
#endif
#ifdef PACKAGE_TARNAME
#define ORIG_PACKAGE_TARNAME PACKAGE_TARNAME
#undef PACKAGE_TARNAME
#endif
#ifdef PACKAGE_VERSION
#define ORIG_PACKAGE_VERSION PACKAGE_VERSION
#undef PACKAGE_VERSION
#endif
#ifdef PACKAGE_STRING
#define ORIG_PACKAGE_STRING PACKAGE_STRING
#undef PACKAGE_STRING
#endif
#ifdef PACKAGE_BUGREPORT
#define ORIG_PACKAGE_BUGREPORT PACKAGE_BUGREPORT
#undef PACKAGE_BUGREPORT
#endif
#ifdef PACKAGE_URL
#define ORIG_PACKAGE_URL PACKAGE_URL
#undef PACKAGE_URL
#endif
#endif /* PIKE_CORE */

#include "machine.h"

#ifndef PIKE_CORE
#undef PACKAGE_NAME
#ifdef ORIG_PACKAGE_NAME
#define PACKAGE_NAME ORIG_PACKAGE_NAME
#undef ORIG_PACKAGE_NAME
#endif
#undef PACKAGE_TARNAME
#ifdef ORIG_PACKAGE_TARNAME
#define PACKAGE_TARNAME ORIG_PACKAGE_TARNAME
#undef ORIG_PACKAGE_TARNAME
#endif
#undef PACKAGE_VERSION
#ifdef ORIG_PACKAGE_VERSION
#define PACKAGE_VERSION ORIG_PACKAGE_VERSION
#undef ORIG_PACKAGE_VERSION
#endif
#undef PACKAGE_STRING
#ifdef ORIG_PACKAGE_STRING
#define PACKAGE_STRING ORIG_PACKAGE_STRING
#undef ORIG_PACKAGE_STRING
#endif
#undef PACKAGE_BUGREPORT
#ifdef ORIG_PACKAGE_BUGREPORT
#define PACKAGE_BUGREPORT ORIG_PACKAGE_BUGREPORT
#undef ORIG_PACKAGE_BUGREPORT
#endif
#undef PACKAGE_URL
#ifdef ORIG_PACKAGE_URL
#define PACKAGE_URL ORIG_PACKAGE_URL
#undef ORIG_PACKAGE_URL
#endif
#endif /* PIKE_CORE */

#endif /* CONFIGURE_TEST */

/* Some identifiers used as flags in the machine.h defines. */
#define PIKE_YES	1
#define PIKE_NO		2
#define PIKE_UNKNOWN	3

/* We want to use errno later */
#ifdef _SGI_SPROC_THREADS
/* Magic define of _SGI_MP_SOURCE above might redefine errno below */
#include <errno.h>
#if defined(HAVE_OSERROR) && !defined(errno)
#define errno (oserror())
#endif /* HAVE_OSERROR && !errno */
#endif /* _SGI_SPROC_THREADS */

/* This macro is only provided for compatibility with
 * Windows PreRelease. Use ALIGNOF() instead!
 * (Needed for va_arg().)
 */
#ifndef __alignof
#define __alignof(X) ((size_t)&(((struct { char ignored_ ; X fooo_; } *)0)->fooo_))
#endif /* __alignof */

#ifdef HAVE_FUNCTION_ATTRIBUTES
#define ATTRIBUTE(X) __attribute__ (X)
#else
#define ATTRIBUTE(X)
#endif

#ifdef HAVE_DECLSPEC
#define DECLSPEC(X) __declspec(X)
#else /* !HAVE_DECLSPEC */
#define DECLSPEC(X)
#endif /* HAVE_DECLSPEC */

#ifdef HAS___BUILTIN_EXPECT
# define UNLIKELY(X) __builtin_expect( (long)(X), 0 )
# define LIKELY(X) __builtin_expect( (long)(X), 1 )
#else
# define UNLIKELY(X) X
# define LIKELY(X) X
#endif

#ifdef HAS___BUILTIN_UNREACHABLE
# define UNREACHABLE(X) __builtin_unreachable()
#else
# define UNREACHABLE(X) X
#endif

#ifndef HAVE_WORKING_REALLOC_NULL
#define realloc(PTR, SZ)	pike_realloc(PTR,SZ)
#endif

/* NOTE:
 *    PIKE_CONCAT doesn't get defined if there isn't any way to
 *    concatenate symbols
 */
#ifdef HAVE_ANSI_CONCAT
#define PIKE_CONCAT(X,Y)	X##Y
#define PIKE_CONCAT3(X,Y,Z)	X##Y##Z
#define PIKE_CONCAT4(X,Y,Z,Q)	X##Y##Z##Q
#else
#ifdef HAVE_KR_CONCAT
#define PIKE_CONCAT(X,Y)	X/**/Y
#define PIKE_CONCAT3(X,Y,Z)	X/**/Y/**/Z
#define PIKE_CONCAT4(X,Y,Z,Q)	X/**/Y/**/Z/**/Q
#endif /* HAVE_KR_CONCAT */
#endif /* HAVE_ANSI_CONCAT */

#define TOSTR(X)	#X
#define DEFINETOSTR(X)	TOSTR(X)

/*
 * Max number of local variables in a function.
 * Currently there is no support for more than 256
 */
#define MAX_LOCAL	256

/*
 * define NO_GC to get rid of garbage collection
 */
#ifndef NO_GC
#define GC2
#endif

#ifdef i386
#ifndef HANDLES_UNALIGNED_MEMORY_ACCESS
#define HANDLES_UNALIGNED_MEMORY_ACCESS
#endif
#endif /* i386 */

/* AIX requires this to be the first thing in the file.  */
#if HAVE_ALLOCA_H
# include <alloca.h>
# ifdef __GNUC__
#  ifdef alloca
#   undef alloca
#  endif
#  define alloca __builtin_alloca
# endif
#else
# ifdef __GNUC__
#  ifdef alloca
#   undef alloca
#  endif
#  define alloca __builtin_alloca
# else
#  ifdef _AIX
#pragma alloca
#  else
#   ifndef alloca /* predefined by HP cc +Olibcalls */
void *alloca();
#   endif
#  endif
# endif
#endif

#ifdef HAVE_DEVICES_TIMER_H
/* On AmigaOS, struct timeval is defined in a variety of places
   and a variety of ways.  Making sure <devices/timer.h> is included
   first brings some amount of order to the chaos. */
#include <devices/timer.h>
#endif

#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stddef.h>
#include <string.h>
#include <limits.h>
#include <float.h>

#ifdef HAVE_MALLOC_H
#if !defined(__FreeBSD__) && !defined(__OpenBSD__)
/* FreeBSD and OpenBSD has <malloc.h>, but it just contains a warning... */
#include <malloc.h>
#endif /* !__FreeBSD__ && !__OpenBSD */
#endif

#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif

#ifdef HAVE_SYS_TYPES_H
#include <sys/types.h>
#endif

#ifdef HAVE_MEMORY_H
#include <memory.h>
#endif

#ifdef HAVE_WINDOWS_H
#include <windows.h>
#endif

/* Get INT64, INT32, INT16, INT8, et al. */
#include "pike_int_types.h"

#define SIZE_T unsigned INT32

#define TYPE_T unsigned int
#define TYPE_FIELD unsigned INT16

#define B1_T char

#if SIZEOF_SHORT == 2
#define B2_T short
#elif SIZEOF_INT == 2
#define B2_T int
#endif

#if SIZEOF_SHORT == 4
#define B4_T short
#elif SIZEOF_INT == 4
#define B4_T int
#elif SIZEOF_LONG == 4
#define B4_T long
#endif

#if SIZEOF_INT == 8
#define B8_T int
#elif SIZEOF_LONG == 8
#define B8_T long
#elif (SIZEOF_LONG_LONG - 0) == 8
#define B8_T long long
#elif (SIZEOF___INT64 - 0) == 8
#define B8_T __int64
#elif SIZEOF_CHAR_P == 8
#define B8_T char *
#elif defined(B4_T)
struct b8_t_s { B4_T x,y; };
#define B8_T struct b8_t_s
#endif

#ifdef B8_T
struct b16_t_s { B8_T x,y; };
#define B16_T struct b16_t_s
#endif

/* INT_TYPE stuff */
#ifndef MAX_INT_TYPE
# ifdef WITH_SHORT_INT

#  define MAX_INT_TYPE	SHRT_MAX
#  define MIN_INT_TYPE	SHRT_MIN
#  define PRINTPIKEINT	"h"
#  define INT_ARG_TYPE	int

# elif defined(WITH_INT_INT)

#  define MAX_INT_TYPE	INT_MAX
#  define MIN_INT_TYPE	INT_MIN
#  define PRINTPIKEINT	""

# elif defined(WITH_LONG_INT)

#  define MAX_INT_TYPE	LONG_MAX
#  define MIN_INT_TYPE	LONG_MIN
#  define PRINTPIKEINT	"l"

# elif defined(WITH_LONG_LONG_INT)

#  ifdef LLONG_MAX
#   define MAX_INT_TYPE	LLONG_MAX
#   define MIN_INT_TYPE	LLONG_MIN
#  else
#   define MAX_INT_TYPE	LONG_LONG_MAX
#   define MIN_INT_TYPE	LONG_LONG_MIN
#  endif
#  define PRINTPIKEINT	"ll"

# endif
#endif

/* INT_ARG_TYPE is a type suitable for argument passing that at least
 * can hold an INT_TYPE value. */
#ifndef INT_ARG_TYPE
#define INT_ARG_TYPE INT_TYPE
#endif

#if SIZEOF_INT_TYPE - 0 == 0
# error Unsupported type chosen for native pike integers.
#endif

#if SIZEOF_INT_TYPE != 4
# define INT_TYPE_INT32_CONVERSION
#endif

/* FLOAT_TYPE stuff */
#ifdef WITH_LONG_DOUBLE_PRECISION_SVALUE

#  define PIKEFLOAT_MANT_DIG	LDBL_MANT_DIG
#  define PIKEFLOAT_DIG		LDBL_DIG
#  define PIKEFLOAT_MIN_EXP	LDBL_MIN_EXP
#  define PIKEFLOAT_MAX_EXP	LDBL_MAX_EXP
#  define PIKEFLOAT_MIN_10_EXP	LDBL_MIN_10_EXP
#  define PIKEFLOAT_MAX_10_EXP	LDBL_MAX_10_EXP
#  define PIKEFLOAT_MAX		LDBL_MAX
#  define PIKEFLOAT_MIN		LDBL_MIN
#  define PIKEFLOAT_EPSILON	LDBL_EPSILON
#  define PRINTPIKEFLOAT	"L"

#elif defined(WITH_DOUBLE_PRECISION_SVALUE)

#  define PIKEFLOAT_MANT_DIG	DBL_MANT_DIG
#  define PIKEFLOAT_DIG		DBL_DIG
#  define PIKEFLOAT_MIN_EXP	DBL_MIN_EXP
#  define PIKEFLOAT_MAX_EXP	DBL_MAX_EXP
#  define PIKEFLOAT_MIN_10_EXP	DBL_MIN_10_EXP
#  define PIKEFLOAT_MAX_10_EXP	DBL_MAX_10_EXP
#  define PIKEFLOAT_MAX		DBL_MAX
#  define PIKEFLOAT_MIN		DBL_MIN
#  define PIKEFLOAT_EPSILON	DBL_EPSILON
#  define PRINTPIKEFLOAT	""

#else

#  define PIKEFLOAT_MANT_DIG	FLT_MANT_DIG
#  define PIKEFLOAT_DIG		FLT_DIG
#  define PIKEFLOAT_MIN_EXP	FLT_MIN_EXP
#  define PIKEFLOAT_MAX_EXP	FLT_MAX_EXP
#  define PIKEFLOAT_MIN_10_EXP	FLT_MIN_10_EXP
#  define PIKEFLOAT_MAX_10_EXP	FLT_MAX_10_EXP
#  define PIKEFLOAT_MAX		FLT_MAX
#  define PIKEFLOAT_MIN		FLT_MIN
#  define PIKEFLOAT_EPSILON	FLT_EPSILON
#  define PRINTPIKEFLOAT	""
#  define FLOAT_ARG_TYPE	double

#endif

/* FLOAT_ARG_TYPE is a type suitable for argument passing that at
 * least can hold a FLOAT_TYPE value. */
#ifndef FLOAT_ARG_TYPE
#define FLOAT_ARG_TYPE FLOAT_TYPE
#endif

#if SIZEOF_FLOAT_TYPE - 0 == 0
#error Unsupported type chosen for pike floats.
#endif

/* Conceptually a char is a 32 bit signed value. Implementationwise
 * that means that the shorter ones don't have space for the sign bit. */
typedef unsigned char p_wchar0;
typedef unsigned INT16 p_wchar1;
typedef signed INT32 p_wchar2;

typedef struct p_wchar_p
{
  void *ptr;
  int shift;
} PCHARP;

#define WERR(...) fprintf(stderr,__VA_ARGS__)

#ifdef PIKE_DEBUG

#define DO_IF_DEBUG(X) X
#define DO_IF_DEBUG_ELSE(DEBUG, NO_DEBUG) DEBUG
#define DWERR(...) WERR(__VA_ARGS__)

#undef NDEBUG

/* Set of macros to simplify passing __FILE__ and __LINE__ to
 * functions only in debug mode. */
#define DLOC			__FILE__, __LINE__
#define COMMA_DLOC		, __FILE__, __LINE__
#define DLOC_DECL		const char *dloc_file, int dloc_line
#define COMMA_DLOC_DECL		, const char *dloc_file, int dloc_line
#define DLOC_ARGS		dloc_file, dloc_line
#define DLOC_ARGS_OPT		dloc_file, dloc_line
#define COMMA_DLOC_ARGS_OPT	, dloc_file, dloc_line
#define USE_DLOC_ARGS()		((void)(DLOC_ARGS_OPT))
#define DLOC_ENABLED

#else  /* !PIKE_DEBUG */

#define DO_IF_DEBUG(X)
#define DO_IF_DEBUG_ELSE(DEBUG, NO_DEBUG) NO_DEBUG
#define DWERR(...)
#define NDEBUG

#define DLOC
#define COMMA_DLOC
#define DLOC_DECL
#define COMMA_DLOC_DECL
#define DLOC_ARGS		__FILE__, __LINE__
#define DLOC_ARGS_OPT
#define COMMA_DLOC_ARGS_OPT
#define USE_DLOC_ARGS()

#endif	/* !PIKE_DEBUG */

#include <assert.h>

#if defined (PIKE_DEBUG) || defined (DO_PIKE_CLEANUP)
#define DO_IF_DEBUG_OR_CLEANUP(X) X
#else
#define DO_IF_DEBUG_OR_CLEANUP(X)
#endif

#ifdef INTERNAL_PROFILING
#define DO_IF_INTERNAL_PROFILING(X) X
#else
#define DO_IF_INTERNAL_PROFILING(X)
#endif

/* Suppress compiler warnings for unused parameters if possible. The mangling of
   argument name is required to catch when an unused argument later is used without
   removing the annotation. */
#ifndef PIKE_UNUSED_ATTRIBUTE
# ifdef __GNUC__
#  define PIKE_UNUSED_ATTRIBUTE  __attribute__((unused))
#  if (__GNUC__ > 3) || (__GNUC__ == 3 && __GNUC_MINOR__ >= 4)
#   define PIKE_WARN_UNUSED_RESULT_ATTRIBUTE  __attribute__((warn_unused_result))
#  else /* GCC < 3.4 */
#   define PIKE_WARN_UNUSED_RESULT_ATTRIBUTE
#  endif
# else
#  define PIKE_UNUSED_ATTRIBUTE
#  define PIKE_WARN_UNUSED_RESULT_ATTRIBUTE
# endif
#endif
#ifndef PIKE_UNUSED
# define PIKE_UNUSED(x)  PIKE_CONCAT(x,_UNUSED) PIKE_UNUSED_ATTRIBUTE
#endif
#ifndef UNUSED
# define UNUSED(x)  PIKE_UNUSED(x)
#endif
#ifdef PIKE_DEBUG
# define DEBUGUSED(x) x
#else
# define DEBUGUSED(x) PIKE_UNUSED(x)
#endif
#ifdef DEBUG_MALLOC
# define DMALLOCUSED(x) x
#else
# define DMALLOCUSED(x) PIKE_UNUSED(x)
#endif


/* PMOD_EXPORT exports a function / variable vfsh. */
#ifndef PMOD_EXPORT
# if defined (__NT__) && defined (USE_DLL)
#  ifdef DYNAMIC_MODULE
#   define PMOD_EXPORT __declspec(dllimport)
#  else
/* A pmod export becomes an import in the dynamic module. This means
 * that modules can't use PMOD_EXPORT for identifiers they export
 * themselves, unless they are compiled statically. */
#   define PMOD_EXPORT __declspec(dllexport)
#  endif
# elif defined(__clang__) && defined(MAC_OS_X_VERSION_MIN_REQUIRED)
/* According to Clang source the protected behavior is ELF-specific and not
   applicable to OS X. */
#  define PMOD_EXPORT    __attribute__ ((visibility("default")))
# elif __GNUC__ >= 4
#  ifdef DYNAMIC_MODULE
#    define PMOD_EXPORT  __attribute__ ((visibility("default")))
#  else
#    define PMOD_EXPORT  __attribute__ ((visibility("protected")))
#  endif
# else
#  define PMOD_EXPORT
# endif
#endif

#ifndef PMOD_PROTO
#define PMOD_PROTO
#endif

#ifndef DO_PIKE_CLEANUP
#if defined(PURIFY) || defined(__CHECKER__) || defined(DEBUG_MALLOC)
#define DO_PIKE_CLEANUP
#endif
#endif

#ifndef HAVE_STRUCT_IOVEC
#define HAVE_STRUCT_IOVEC
struct iovec {
  void *iov_base;
  size_t iov_len;
};
#endif /* !HAVE_STRUCT_IOVEC */

#include "port.h"
#include "dmalloc.h"

/* Either this include must go or the include of threads.h in
 * pike_cpulib.h. Otherwise we get pesky include loops. */
/* #include "pike_cpulib.h" */

#ifdef MALLOC_DECL_MISSING
void *malloc (int);
void *realloc (void *,int);
void free (void *);
void *calloc (int,int);
#endif

#ifdef GETPEERNAME_DECL_MISSING
int getpeername (int, struct sockaddr *, int *);
#endif

#ifdef GETHOSTNAME_DECL_MISSING
void gethostname (char *,int);
#endif

#ifdef POPEN_DECL_MISSING
FILE *popen (char *,char *);
#endif

#ifdef GETENV_DECL_MISSING
char *getenv (char *);
#endif

#ifdef USE_CRYPT_C
char *crypt(const char *, const char *);
#endif /* USE_CRYPT_C */

/* If this define is present, error() has been renamed to Pike_error() and
 * error.h has been renamed to pike_error.h
 * Expect to see other similar defines in the future. -Hubbe
 */
#define Pike_error_present

/* Compatibility... */
#define USE_PIKE_TYPE	2

/* Used in more than one place, better put it here */
#ifdef PROFILING
#define DO_IF_PROFILING(X) X
#else
#define DO_IF_PROFILING(X)
#endif

/* #define PROFILING_DEBUG */

#ifdef PROFILING_DEBUG
#define W_PROFILING_DEBUG(...) WERR(__VA_ARGS__)
#else /* !PROFILING_DEBUG */
#define W_PROFILING_DEBIG(...)
#endif /* PROFILING_DEBUG */

#endif
