__LIBCL_OVERLOAD float __libcl_exp10_impl(float x);
__LIBCL_OVERLOAD double __libcl_exp10_impl(double x);

#define __LIBCL_GENTYPE__ float
#define __LIBCL_FUNC__ exp10
#include <vectorize.h>

#define __LIBCL_GENTYPE__ double
#define __LIBCL_FUNC__ exp10
#include <vectorize.h>
