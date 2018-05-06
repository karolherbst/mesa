__LIBCL_OVERLOAD float __libcl_hypot_impl(float x, float y)
{
   uint ux = as_uint(x);
   uint aux = ux & EXSIGNBIT_SP32;
   uint uy = as_uint(y);
   uint auy = uy & EXSIGNBIT_SP32;
   float retval;
   int c = aux > auy;
   ux = c ? aux : auy;
   uy = c ? auy : aux;

   int xexp = clamp((int)(ux >> EXPSHIFTBITS_SP32) - EXPBIAS_SP32, -126, 126);
   float fx_exp = as_float((xexp + EXPBIAS_SP32) << EXPSHIFTBITS_SP32);
   float fi_exp = as_float((-xexp + EXPBIAS_SP32) << EXPSHIFTBITS_SP32);
   float fx = as_float(ux) * fi_exp;
   float fy = as_float(uy) * fi_exp;
   retval = sqrt(mad(fx, fx, fy*fy)) * fx_exp;

   retval = ux > PINFBITPATT_SP32 | uy == 0 ? as_float(ux) : retval;
   retval = ux == PINFBITPATT_SP32 | uy == PINFBITPATT_SP32 ? as_float(PINFBITPATT_SP32) : retval;
   return retval;
}

__LIBCL_OVERLOAD double __libcl_hypot_impl(double x, double y)
{
   ulong ux = as_ulong(x) & ~SIGNBIT_DP64;
   int xexp = ux >> EXPSHIFTBITS_DP64;
   x = as_double(ux);

   ulong uy = as_ulong(y) & ~SIGNBIT_DP64;
   int yexp = uy >> EXPSHIFTBITS_DP64;
   y = as_double(uy);

   int c = xexp > EXPBIAS_DP64 + 500 | yexp > EXPBIAS_DP64 + 500;
   double preadjust = c ? 0x1.0p-600 : 1.0;
   double postadjust = c ? 0x1.0p+600 : 1.0;

   c = xexp < EXPBIAS_DP64 - 500 | yexp < EXPBIAS_DP64 - 500;
   preadjust = c ? 0x1.0p+600 : preadjust;
   postadjust = c ? 0x1.0p-600 : postadjust;

   double ax = x * preadjust;
   double ay = y * preadjust;

   // The post adjust may overflow, but this can't be avoided in any case
   double r = sqrt(fma(ax, ax, ay*ay)) * postadjust;

   // If the difference in exponents between x and y is large
   double s = x + y;
   c = abs(xexp - yexp) > MANTLENGTH_DP64 + 1;
   r = c ? s : r;

   // Check for NaN
   //c = x != x | y != y;
   c = isnan(x) | isnan(y);
   r = c ? as_double(QNANBITPATT_DP64) : r;

   // If either is Inf, we must return Inf
   c = x == as_double(PINFBITPATT_DP64) | y == as_double(PINFBITPATT_DP64);
   r = c ? as_double(PINFBITPATT_DP64) : r;

   return r;
}
