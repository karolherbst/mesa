// this is stupid
__LIBCL_INLINE __LIBCL_OVERLOAD __LIBCL_GENTYPE__ __LIBCL_FUNC__(__LIBCL_GENTYPE__ x)
{
	return __LIBCL_FUNC_CALL(__LIBCL_FUNC__)(x);
}

__LIBCL_INLINE __LIBCL_OVERLOAD __LIBCL_VEC_TYPE(2) __LIBCL_FUNC__(__LIBCL_VEC_TYPE(2) x)
{
	__LIBCL_VEC_TYPE(2) result;
	result.lo = __LIBCL_FUNC__(x.lo);
	result.hi = __LIBCL_FUNC__(x.hi);
	return result;
}

__LIBCL_INLINE __LIBCL_OVERLOAD __LIBCL_VEC_TYPE(3) __LIBCL_FUNC__(__LIBCL_VEC_TYPE(3) x)
{
	__LIBCL_VEC_TYPE(3) result;
	result.s01 = __LIBCL_FUNC__(x.s01);
	result.s2 = __LIBCL_FUNC__(x.s2);
	return result;
}

__LIBCL_INLINE __LIBCL_OVERLOAD __LIBCL_VEC_TYPE(4) __LIBCL_FUNC__(__LIBCL_VEC_TYPE(4) x)
{
	__LIBCL_VEC_TYPE(4) result;
	result.lo = __LIBCL_FUNC__(x.lo);
	result.hi = __LIBCL_FUNC__(x.hi);
	return result;
}

__LIBCL_INLINE __LIBCL_OVERLOAD __LIBCL_VEC_TYPE(8) __LIBCL_FUNC__(__LIBCL_VEC_TYPE(8) x)
{
	__LIBCL_VEC_TYPE(8) result;
	result.lo = __LIBCL_FUNC__(x.lo);
	result.hi = __LIBCL_FUNC__(x.hi);
	return result;
}

__LIBCL_INLINE __LIBCL_OVERLOAD __LIBCL_VEC_TYPE(16) __LIBCL_FUNC__(__LIBCL_VEC_TYPE(16) x)
{
	__LIBCL_VEC_TYPE(16) result;
	result.lo = __LIBCL_FUNC__(x.lo);
	result.hi = __LIBCL_FUNC__(x.hi);
	return result;
}

#undef __LIBCL_GENTYPE__
#undef __LIBCL_FUNC__

