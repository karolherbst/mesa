// this is stupid
__LIBCL_INLINE __LIBCL_OVERLOAD __LIBCL_GENTYPE__ __LIBCL_FUNC__(__LIBCL_GENTYPE__ x, __LIBCL_GENTYPE__ y)
{
	return __LIBCL_FUNC_CALL(__LIBCL_FUNC__)(x, y);
}

__LIBCL_INLINE __LIBCL_OVERLOAD __LIBCL_VEC_TYPE(2) __LIBCL_FUNC__(__LIBCL_VEC_TYPE(2) x, __LIBCL_VEC_TYPE(2) y)
{
	__LIBCL_VEC_TYPE(2) result;
	result.lo = __LIBCL_FUNC_CALL(__LIBCL_FUNC__)(x.lo, y.lo);
	result.hi = __LIBCL_FUNC_CALL(__LIBCL_FUNC__)(x.hi, y.hi);
	return result;
}

__LIBCL_INLINE __LIBCL_OVERLOAD __LIBCL_VEC_TYPE(3) __LIBCL_FUNC__(__LIBCL_VEC_TYPE(3) x, __LIBCL_VEC_TYPE(3) y)
{
	__LIBCL_VEC_TYPE(3) result;
	result.s01 = __LIBCL_FUNC__(x.s01, y.s01);
	result.s2 = __LIBCL_FUNC_CALL(__LIBCL_FUNC__)(x.s2, y.s2);
	return result;
}

__LIBCL_INLINE __LIBCL_OVERLOAD __LIBCL_VEC_TYPE(4) __LIBCL_FUNC__(__LIBCL_VEC_TYPE(4) x, __LIBCL_VEC_TYPE(4) y)
{
	__LIBCL_VEC_TYPE(4) result;
	result.lo = __LIBCL_FUNC__(x.lo, y.lo);
	result.hi = __LIBCL_FUNC__(x.hi, y.hi);
	return result;
}

__LIBCL_INLINE __LIBCL_OVERLOAD __LIBCL_VEC_TYPE(8) __LIBCL_FUNC__(__LIBCL_VEC_TYPE(8) x, __LIBCL_VEC_TYPE(8) y)
{
	__LIBCL_VEC_TYPE(8) result;
	result.lo = __LIBCL_FUNC__(x.lo, y.lo);
	result.hi = __LIBCL_FUNC__(x.hi, y.hi);
	return result;
}

__LIBCL_INLINE __LIBCL_OVERLOAD __LIBCL_VEC_TYPE(16) __LIBCL_FUNC__(__LIBCL_VEC_TYPE(16) x, __LIBCL_VEC_TYPE(16) y)
{
	__LIBCL_VEC_TYPE(16) result;
	result.lo = __LIBCL_FUNC__(x.lo, y.lo);
	result.hi = __LIBCL_FUNC__(x.hi, y.hi);
	return result;
}

#undef __LIBCL_GENTYPE__
#undef __LIBCL_FUNC__

