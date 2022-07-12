import subprocess
import re

# \file gen_replace_math.py
# Generate dg_replace_math.c.

class DERIVGRIND_MATH_FUNCTION_BASE:
  def __init__(self,name,type_):
    self.name = name
    self.type = type_
    if self.type=="double":
      self.size = 8
      self.T = "D"
    elif self.type=="float":
      self.size = 4
      self.T = "F"
    elif self.type=="long double":
      self.size = 10
      self.T = "L"
    else:
      print("Unknown type '"+self.type+"'")
      exit(1)

class DERIVGRIND_MATH_FUNCTION(DERIVGRIND_MATH_FUNCTION_BASE):
  """Wrap a math.h function (type)->type to also handle
    the derivative information."""
  def __init__(self,name,deriv,type_):
    super().__init__(name,type_)
    self.deriv = deriv
  def c_code(self):
    return \
f"""
__attribute__((optimize("O0")))
{self.type} I_WRAP_SONAME_FNNAME_ZU(libmZdsoZa, {self.name}) ({self.type} x) {{
  OrigFn fn;
  VALGRIND_GET_ORIG_FN(fn);
  {self.type} ret;
  CALL_FN_{self.T}_{self.T}(ret, fn, x);
  if(!called_from_within_wrapper) {{
    {self.type} x_d;
    VALGRIND_GET_DERIVATIVE(&x, &x_d, {self.size});
    called_from_within_wrapper = true;
      {self.type} ret_d = ({self.deriv}) * x_d;
    called_from_within_wrapper = false;
    VALGRIND_SET_DERIVATIVE(&ret, &ret_d, {self.size});
  }}
  return ret;
}}
"""

class DERIVGRIND_MATH_FUNCTION2(DERIVGRIND_MATH_FUNCTION_BASE):
  """Wrap a math.h function (type,type)->type to also handle
    the derivative information."""
  def __init__(self,name,derivX,derivY,type_):
    super().__init__(name,type_)
    self.derivX = derivX
    self.derivY = derivY
  def c_code(self):
    return \
f"""
__attribute__((optimize("O0")))
{self.type} I_WRAP_SONAME_FNNAME_ZU(libmZdsoZa, {self.name}) ({self.type} x, {self.type} y) {{
  OrigFn fn;
  VALGRIND_GET_ORIG_FN(fn);
  {self.type} ret;
  CALL_FN_{self.T}_{self.T}{self.T}(ret, fn, x, y);
  if(!called_from_within_wrapper) {{
    {self.type} x_d, y_d;
    VALGRIND_GET_DERIVATIVE(&x, &x_d, {self.size});
    VALGRIND_GET_DERIVATIVE(&y, &y_d, {self.size});
    called_from_within_wrapper = true;
      {self.type} ret_d = ({self.derivX}) * x_d + ({self.derivY}) * y_d;
    called_from_within_wrapper = false;
    VALGRIND_SET_DERIVATIVE(&ret, &ret_d, {self.size});
  }}
  return ret;
}}
"""

functions = [

  # missing: modf
  DERIVGRIND_MATH_FUNCTION("acos","-1./sqrt(1.-x*x)","double"),
  DERIVGRIND_MATH_FUNCTION("asin","1./sqrt(1.-x*x)","double"),
  DERIVGRIND_MATH_FUNCTION("atan","1./(1.+x*x)","double"),
  DERIVGRIND_MATH_FUNCTION("ceil","0.","double"),
  DERIVGRIND_MATH_FUNCTION("cos", "-sin(x)","double"),
  DERIVGRIND_MATH_FUNCTION("cosh", "sinh(x)","double"),
  DERIVGRIND_MATH_FUNCTION("exp", "exp(x)","double"),
  DERIVGRIND_MATH_FUNCTION("fabs", "(x>0.?1.:-1.)","double"),
  DERIVGRIND_MATH_FUNCTION("floor", "0.","double"),
  DERIVGRIND_MATH_FUNCTION("log","1./x","double"),
  DERIVGRIND_MATH_FUNCTION("log10", "1./(log(10.)*x)","double"),
  DERIVGRIND_MATH_FUNCTION("sin", "cos(x)","double"),
  DERIVGRIND_MATH_FUNCTION("sinh", "cosh(x)","double"),
  DERIVGRIND_MATH_FUNCTION("sqrt", "1./(2.*sqrt(x))","double"),
  DERIVGRIND_MATH_FUNCTION("tan", "1./(cos(x)*cos(x))","double"),
  DERIVGRIND_MATH_FUNCTION("tanh", "1.-tanh(x)*tanh(x)","double"),
  DERIVGRIND_MATH_FUNCTION2("atan2","-y/(x*x+y*y)","x/(x*x+y*y)","double"),


  DERIVGRIND_MATH_FUNCTION("acosf","-1.f/sqrtf(1.f-x*x)","float"),
  DERIVGRIND_MATH_FUNCTION("asinf","1.f/sqrtf(1.f-x*x)","float"),
  DERIVGRIND_MATH_FUNCTION("atanf","1.f/(1.f+x*x)","float"),
  DERIVGRIND_MATH_FUNCTION("ceilf","0.f","float"),
  DERIVGRIND_MATH_FUNCTION("cosf", "-sinf(x)","float"),
  DERIVGRIND_MATH_FUNCTION("coshf", "sinhf(x)","float"),
  DERIVGRIND_MATH_FUNCTION("expf", "expf(x)","float"),
  DERIVGRIND_MATH_FUNCTION("fabsf", "(x>0.f?1.f:-1.f)","float"),
  DERIVGRIND_MATH_FUNCTION("floorf", "0.f","float"),
  DERIVGRIND_MATH_FUNCTION("logf","1.f/x","float"),
  DERIVGRIND_MATH_FUNCTION("log10f", "1.f/(logf(10.f)*x)","float"),
  DERIVGRIND_MATH_FUNCTION("sinf", "cosf(x)","float"),
  DERIVGRIND_MATH_FUNCTION("sinhf", "coshf(x)","float"),
  DERIVGRIND_MATH_FUNCTION("sqrtf", "1.f/(2.f*sqrtf(x))","float"),
  DERIVGRIND_MATH_FUNCTION("tanf", "1.f/(cosf(x)*cosf(x))","float"),
  DERIVGRIND_MATH_FUNCTION("tanhf", "1.f-tanhf(x)*tanhf(x)","float"),
  DERIVGRIND_MATH_FUNCTION2("atan2f","-y/(x*x+y*y)","x/(x*x+y*y)","float"),

  DERIVGRIND_MATH_FUNCTION("acosl","-1.l/sqrtl(1.l-x*x)","long double"),
  DERIVGRIND_MATH_FUNCTION("asinl","1.l/sqrtl(1.l-x*x)","long double"),
  DERIVGRIND_MATH_FUNCTION("atanl","1.l/(1.l+x*x)","long double"),
  DERIVGRIND_MATH_FUNCTION("ceill","0.l","long double"),
  DERIVGRIND_MATH_FUNCTION("cosl", "-sinl(x)","long double"),
  DERIVGRIND_MATH_FUNCTION("coshl", "sinhl(x)","long double"),
  DERIVGRIND_MATH_FUNCTION("expl", "expl(x)","long double"),
  DERIVGRIND_MATH_FUNCTION("fabsl", "(x>0.l?1.l:-1.l)","long double"),
  DERIVGRIND_MATH_FUNCTION("floorl", "0.l","long double"),
  DERIVGRIND_MATH_FUNCTION("logl","1.l/x","long double"),
  DERIVGRIND_MATH_FUNCTION("log10l", "1.l/(logl(10.l)*x)","long double"),
  DERIVGRIND_MATH_FUNCTION("sinl", "cosl(x)","long double"),
  DERIVGRIND_MATH_FUNCTION("sinhl", "coshl(x)","long double"),
  DERIVGRIND_MATH_FUNCTION("sqrtl", "1.l/(2.l*sqrtl(x))","long double"),
  DERIVGRIND_MATH_FUNCTION("tanl", "1.l/(cosl(x)*cosl(x))","long double"),
  DERIVGRIND_MATH_FUNCTION("tanhl", "1.l-tanhl(x)*tanhl(x)","long double"),
  DERIVGRIND_MATH_FUNCTION2("atan2l","-y/(x*x+y*y)","x/(x*x+y*y)","long double"),
]


with open("dg_replace_math.c","w") as f:
  f.write("""
/*--------------------------------------------------------------------*/
/*--- Math library wrapper.                      dg_replace_math.c ---*/
/*--------------------------------------------------------------------*/

/*
   This file is part of DerivGrind, a tool performing forward-mode
   algorithmic differentiation of compiled programs implemented
   in the Valgrind framework.

   Copyright (C) 2022 Max Aehle
      max.aehle@scicomp.uni-kl.de

   This program is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public License as
   published by the Free Software Foundation; either version 2 of the
   License, or (at your option) any later version.

   This program is distributed in the hope that it will be useful, but
   WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, see <http://www.gnu.org/licenses/>.

   The GNU General Public License is contained in the file COPYING.
*/

// ----------------------------------------------------
// WARNING: This file has been generated by 
// gen_replace_math.py. Do not manually edit it.
// ----------------------------------------------------

#include "valgrind.h"
#include "derivgrind.h" 
#include <stdbool.h>
#include <math.h>

static bool called_from_within_wrapper = false;
""")
  for function in functions:
    f.write(function.c_code())

