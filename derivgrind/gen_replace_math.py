# -------------------------------------------------------------------- #
# --- Generation of the math wrapper code.     gen_replace_math.py --- #
# -------------------------------------------------------------------- #
#
#  This file is part of Derivgrind, an automatic differentiation
#  tool applicable to compiled programs.
#
#  Copyright (C) 2022, Chair for Scientific Computing, TU Kaiserslautern
#  Copyright (C) since 2023, Chair for Scientific Computing, University of Kaiserslautern-Landau
#  Homepage: https://www.scicomp.uni-kl.de
#  Contact: Prof. Nicolas R. Gauger (derivgrind@projects.rptu.de)
#
#  Lead developer: Max Aehle
#
#  This program is free software; you can redistribute it and/or
#  modify it under the terms of the GNU General Public License as
#  published by the Free Software Foundation; either version 2 of the
#  License, or (at your option) any later version.
#
#  This program is distributed in the hope that it will be useful, but
#  WITHOUT ANY WARRANTY; without even the implied warranty of
#  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
#  General Public License for more details.
#
#  You should have received a copy of the GNU General Public License
#  along with this program; if not, see <http://www.gnu.org/licenses/>.
#
#  The GNU General Public License is contained in the file COPYING.
#

import subprocess
import re

## \page math_wrapping Wrapping of math library functions.
# 
# GLIBC's implementation of the math.h functions uses a lot
# of "bit-tricks", i.e. apply non-floating-point operations to 
# floating-point data, to manipulate them in an arithmetically 
# meaningful and often differentiable way. Derivgrind does not
# recognize most of these bit-tricks. 
#
# We therefore use Valgrind's function wrapping mechanism to
# intercept calls to math.h functions and provide analytical
# derivative information to Derivgrind by client requests.
#
# In forward mode, we obtain the dot values of the operands
# with client requests, calculate the dot value of the result 
# (potentially using math.h functions for the partial 
# derivatives) and set the dot value of the return value 
# with another client request.
#
# In recording mode, we obtain the indices of the operands
# with client requests, calculate the partial derivatives
# of the result w.r.t. the operands (potentially using math.h
# functions), push a new entry with the indices and partial
# derivatives onto the tape, and set the index of the return 
# value with another client request.
#
# We use a static bit to make sure that in the calculation 
# of partial derivatives via math.h functions, we do not
# recursively compute partial derivatives of second, third, ...
# order.
#
# The C code is produced by gen_replace_math.py.
#

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
  """Wrap a math.h function (fp type)->fp type to also handle
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
  bool already_disabled = DG_DISABLE(1,0)!=0;
  {self.type} ret;
  CALL_FN_{self.T}_{self.T}(ret, fn, x);
  double ret_d = ret;
  if(!already_disabled) {{
    if(DG_GET_MODE=='d'){{ /* forward mode */
      {self.type} x_d;
      DG_GET_DOTVALUE(&x, &x_d, {self.size});
      {self.type} ret_d = ({self.deriv}) * x_d;
      DG_SET_DOTVALUE(&ret, &ret_d, {self.size});
      DG_DISABLE(0,1);
    }} else if(DG_GET_MODE=='b') {{ /* recording mode */
      unsigned long long x_i, y_i=0;
      DG_GET_INDEX(&x, &x_i);
      double x_pdiff, y_pdiff=0.;
      x_pdiff = ({self.deriv});
      unsigned long long ret_i;
      DG_DISABLE(0,1);
      DG_NEW_INDEX(&x_i,&y_i,&x_pdiff,&y_pdiff,&ret_i,&ret_d);
      DG_SET_INDEX(&ret,&ret_i);
    }} else if(DG_GET_MODE=='t') {{ /* bit-trick-finding mode */
      DG_DISABLE(0,1);
      unsigned long long xA[2]={{0,0}}, xD[2]={{0,0}};
      DG_GET_FLAGS(&x, xA, xD, {self.size});
      dg_trick_warn_clientcode(xA, xD, {self.size});
      if(xA[0]!=0||xA[1]!=0) xA[0] = xA[1] = 0xfffffffffffffffful;
      xD[0] = xD[1] = 0;
      DG_SET_FLAGS(&ret, xA, xD, {self.size});
    }}
  }} else {{
    DG_DISABLE(0,1);
  }}
  return ret;
}}
"""

class DERIVGRIND_MATH_FUNCTION2(DERIVGRIND_MATH_FUNCTION_BASE):
  """Wrap a math.h function (fp type,fp type)->fp type to also handle
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
  bool already_disabled = DG_DISABLE(1,0);
  {self.type} ret;
  CALL_FN_{self.T}_{self.T}{self.T}(ret, fn, x, y);
  double ret_d = ret;
  if(!already_disabled) {{
    if(DG_GET_MODE=='d'){{ /* forward mode */
      {self.type} x_d, y_d;
      DG_GET_DOTVALUE(&x, &x_d, {self.size});
      DG_GET_DOTVALUE(&y, &y_d, {self.size});
      {self.type} ret_d = ({self.derivX}) * x_d + ({self.derivY}) * y_d;
      DG_SET_DOTVALUE(&ret, &ret_d, {self.size});
      DG_DISABLE(0,1);
    }} else if(DG_GET_MODE=='b') {{ /* recording mode */
      unsigned long long x_i, y_i;
      DG_GET_INDEX(&x,&x_i);
      DG_GET_INDEX(&y,&y_i);
      double x_pdiff, y_pdiff;
      x_pdiff = ({self.derivX});
      y_pdiff = ({self.derivY});
      unsigned long long ret_i;
      DG_DISABLE(0,1);
      DG_NEW_INDEX(&x_i,&y_i,&x_pdiff,&y_pdiff,&ret_i,&ret_d);
      DG_SET_INDEX(&ret,&ret_i);
    }} else if(DG_GET_MODE=='t') {{ /* bit-trick-finding mode */
      DG_DISABLE(0,1);
      unsigned long long xA[2]={{0,0}}, xD[2]={{0,0}};
      unsigned long long yA[2]={{0,0}}, yD[2]={{0,0}};
      DG_GET_FLAGS(&x, xA, xD, {self.size});
      DG_GET_FLAGS(&y, yA, yD, {self.size});
      dg_trick_warn_clientcode(xA, xD, {self.size});
      dg_trick_warn_clientcode(yA, yD, {self.size});
      if(xA[0]!=0||xA[1]!=0||yA[0]!=0||yA[1]!=0) xA[0] = xA[1] = 0xfffffffffffffffful;
      xD[0] = xD[1] = 0;
      DG_SET_FLAGS(&ret, xA, xD, {self.size});
    }}
  }} else {{
    DG_DISABLE(0,1);
  }}
  return ret;
}}
"""

class DERIVGRIND_MATH_FUNCTION2x(DERIVGRIND_MATH_FUNCTION_BASE):
  """Wrap a math.h function (fp type,extra type)->fp type to also handle
    the derivative information."""
  def __init__(self,name,deriv,type_, extratype, extratypeletter):
    super().__init__(name,type_)
    self.deriv = deriv
    self.extratype = extratype
    self.extratypeletter = extratypeletter # 'p' for pointer, 'i' for integer
  def c_code(self):
    return \
f"""
__attribute__((optimize("O0")))
{self.type} I_WRAP_SONAME_FNNAME_ZU(libmZdsoZa, {self.name}) ({self.type} x, {self.extratype} e) {{
  OrigFn fn;
  VALGRIND_GET_ORIG_FN(fn);
  bool already_disabled = DG_DISABLE(1,0);
  {self.type} ret;
  CALL_FN_{self.T}_{self.T}{self.extratypeletter}(ret, fn, x, e);
  double ret_d = ret;
  if(!already_disabled) {{
    if(DG_GET_MODE=='d'){{ /* forward mode */
      {self.type} x_d;
      DG_GET_DOTVALUE(&x, &x_d, {self.size});
      {self.type} ret_d = ({self.deriv}) * x_d;
      DG_SET_DOTVALUE(&ret, &ret_d, {self.size});
      DG_DISABLE(0,1);
    }} else if(DG_GET_MODE=='b') {{ /* recording mode */
      unsigned long long x_i, y_i=0;
      DG_GET_INDEX(&x, &x_i);
      double x_pdiff, y_pdiff=0.;
      x_pdiff = ({self.deriv});
      unsigned long long ret_i;
      DG_DISABLE(0,1);
      DG_NEW_INDEX(&x_i,&y_i,&x_pdiff,&y_pdiff,&ret_i,&ret_d);
      DG_SET_INDEX(&ret,&ret_i);
    }} else if(DG_GET_MODE=='t') {{ /* bit-trick-finding mode */
      DG_DISABLE(0,1);
      unsigned long long xA[2]={{0,0}}, xD[2]={{0,0}};
      DG_GET_FLAGS(&x, xA, xD, {self.size});
      dg_trick_warn_clientcode(xA, xD, {self.size});
      if(xA[0]!=0||xA[1]!=0) xA[0] = xA[1] = 0xfffffffffffffffful;
      xD[0] = xD[1] = 0;
      DG_SET_FLAGS(&ret, xA, xD, {self.size});
    }}
  }} else {{
    DG_DISABLE(0,1);
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
  DERIVGRIND_MATH_FUNCTION2("fmod", "1.", "- floor(fabs(x/y)) * (x>0.?1.:-1.) * (y>0.?1.:-1.)","double"),
  DERIVGRIND_MATH_FUNCTION2("pow"," (y==0.||y==-0.)?0.:(y*pow(x,y-1))", "(x<=0.) ? 0. : (pow(x,y)*log(x))","double"), 
  DERIVGRIND_MATH_FUNCTION2x("frexp","ldexp(1.,-*e)","double","int*","p"),
  DERIVGRIND_MATH_FUNCTION2x("ldexp","ldexp(1.,e)","double","int","i"),
  DERIVGRIND_MATH_FUNCTION2("copysign", "((x>=0.)^(y>=0.)?-1.:1.)", "0.", "double"),



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
  DERIVGRIND_MATH_FUNCTION2("fmodf", "1.f", "- floorf(fabsf(x/y)) * (x>0.f?1.f:-1.f) * (y>0.f?1.f:-1.f)","float"),
  DERIVGRIND_MATH_FUNCTION2("powf"," (y==0.f||y==-0.f)?0.f:(y*powf(x,y-1))", "(x<=0.f) ? 0.f : (powf(x,y)*logf(x))","float"), 
  DERIVGRIND_MATH_FUNCTION2x("frexpf","ldexpf(1.f,-*e)","float","int*","p"),
  DERIVGRIND_MATH_FUNCTION2x("ldexpf","ldexpf(1.f,e)","float","int","i"),
  DERIVGRIND_MATH_FUNCTION2("copysignf", "((x>=0.f)^(y>=0.f)?-1.f:1.f)", "0.f", "float"),


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
  DERIVGRIND_MATH_FUNCTION2("fmodl", "1.l", "- floorl(fabsl(x/y)) * (x>0.l?1.l:-1.l) * (y>0.l?1.l:-1.l)","long double"),
  DERIVGRIND_MATH_FUNCTION2("powl"," (y==0.l||y==-0.l)?0.l:(y*powl(x,y-1))", "(x<=0.l) ? 0.l : (powl(x,y)*logl(x))","long double"), 
  DERIVGRIND_MATH_FUNCTION2x("frexpl","ldexpl(1.l,-*e)","long double","int*","p"),
  DERIVGRIND_MATH_FUNCTION2x("ldexpl","ldexpl(1.l,e)","long double","int","i"),
  DERIVGRIND_MATH_FUNCTION2("copysignl", "((x>=0.l)^(y>=0.l)?-1.l:1.l)", "0.l", "long double"),
]


with open("dg_replace_math.c","w") as f:
  f.write("""
/*--------------------------------------------------------------------*/
/*--- Math library wrapper.                      dg_replace_math.c ---*/
/*--------------------------------------------------------------------*/

/*
   This file is part of Derivgrind, an automatic differentiation
   tool applicable to compiled programs.

   Copyright (C) 2022, Chair for Scientific Computing, TU Kaiserslautern
   Copyright (C) since 2023, Chair for Scientific Computing, University of Kaiserslautern-Landau
   Homepage: https://www.scicomp.uni-kl.de
   Contact: Prof. Nicolas R. Gauger (derivgrind@projects.rptu.de)

   Lead developer: Max Aehle

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
#include <stdio.h>
#include <execinfo.h>

static void dg_trick_warn_clientcode(unsigned long long* fLo, unsigned long long* fHi, unsigned long long size){
  bool warn=false;
  unsigned long long i;
  for(i=0; i<size; i++){
    if( ((unsigned char*)fLo)[i] & ((unsigned char*)fHi)[i] ){
      warn = true;
    }
  }

  if(warn){
    printf("Active discrete data used as floating-point argument to math function.\\n");
    printf("Activity bits: 0x"); 
    for(i=size-1; i>=0; i--) printf("%02X", ((unsigned char*)fLo)[i]); 
    printf(". Discreteness bits: 0x"); 
    for(i=size-1; i>=0; i--) printf("%02X", ((unsigned char*)fHi)[i]); 
    printf("\\nAt\\n");
    void* buffer[50];
    int levels = backtrace(buffer, 50);
    backtrace_symbols_fd(buffer,levels,2);
    printf("\\n");
  }
}

""")
  for function in functions:
    f.write(function.c_code())

