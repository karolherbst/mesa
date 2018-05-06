__LIBCL_OVERLOAD float __libcl_hypot_impl(float x, float y);
__LIBCL_OVERLOAD double __libcl_hypot_impl(double x, double y);

#define __LIBCL_GENTYPE__ float
#define __LIBCL_FUNC__ hypot
#include <vectorize2.h>

#define __LIBCL_GENTYPE__ double
#define __LIBCL_FUNC__ hypot
#include <vectorize2.h>
