/* -------------------------------------------

	Copyright ZKA Technologies

------------------------------------------- */

#ifndef __MPCC_DEF__
#define __MPCC_DEF__

#ifndef __GNUC__

typedef __SIZE_TYPE__ size_t;

#ifdef __LP64__
typedef long int ssize_t;
#else
typedef int ssize_t;
#endif // __LP64__

typedef size_t ptrdiff_t;
typedef size_t uintptr_t;
typedef void*  voidptr_t;
typedef void*  any_t;
typedef char*  caddr_t;

#ifndef NULL
#define NULL ((voidptr_t)0)
#endif // !null

#ifdef __GNUC__
#include <CRT/__mpcc_alloca.hxx>
#define __mpcc_alloca(sz) __mpcc_alloca_gcc(sz)
#define __packed__ __attribute__((packed))
#elif defined(__MPCC__)
#define __packed__ __mpcc_packed__
#define __alloca(sz) __mpcc_alloca(sz)
#endif

#define __deref(ptr) (*(ptr))

#ifdef __cplusplus
#define __init_decl() \
	extern "C"        \
	{


#define __fini_decl() \
	};				  \


#else
#define __init_decl()
#define __fini_decl()
#endif

typedef long long		   off_t;
typedef unsigned long long uoff_t;

typedef union float_cast {
	struct
	{
		unsigned int mantissa : 23;
		unsigned int exponent : 8;
		unsigned int sign : 1;
	};

	float f;
} __packed__ float_cast_t;

typedef union double_cast {
	struct
	{
		unsigned long long int mantissa : 52;
		unsigned int		   exponent : 11;
		unsigned int		   sign : 1;
	};

	double f;
} __packed__ double_cast_t;

#endif // ifndef __GNUC__

#endif /* __MPCC_DEF__ */