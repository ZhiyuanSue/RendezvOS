#ifndef _SHAMPOOS_STDARG_H_
#define _SHAMPOOS_STDARG_H_
#ifdef _GCC_
typedef __builtin_va_list	va_list;
#define va_start(v,l)   __builtin_va_start(v,l)
#define va_end(v)   __builtin_va_end(v)
#define va_arg(v,l) __builtin_va_arg(v,l)
#else
typedef char* va_list;
#define _INTSIZEOF(n) \
	( (sizeof(n) + sizeof(int) - 1) & ~ ( sizeof(int) - 1) )
#define va_start(ap,format) \
	( ap = (va_list)& format + _INTSIZEOF(format))

#define va_end(ap) \
	(ap = (va_list)0)
#define va_arg(ap,type) \
	( *(type*)((ap += _INTSIZEOF(type)) - _INTSIZEOF(type)))

#endif
#endif