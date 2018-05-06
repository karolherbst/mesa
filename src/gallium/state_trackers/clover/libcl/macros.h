#define __LIBCL_INLINE __attribute__((always_inline))
#define __LIBCL_OVERLOAD __attribute__((overloadable))

#define EXPBIAS_SP32      127
#define EXPBIAS_DP64      1023
#define EXPSHIFTBITS_SP32 23
#define EXPSHIFTBITS_DP64 52
#define EXSIGNBIT_SP32    0x7fffffff
#define MANTLENGTH_DP64   53
#define PINFBITPATT_SP32  0x7f800000
#define PINFBITPATT_DP64  0x7ff0000000000000L
#define QNANBITPATT_DP64  0x7ff8000000000000L
#define SIGNBIT_DP64      0x8000000000000000L

#define __LIBCL_EVAL2(x,y,z) x ## y ## z
#define __LIBCL_EVAL(x,y,z) __LIBCL_EVAL2(x,y,z)

#define __LIBCL_VEC_TYPE(nr) __LIBCL_EVAL(__LIBCL_GENTYPE__, nr,)
#define __LIBCL_FUNC_CALL(func) __LIBCL_EVAL2(__libcl_, func, _impl)
