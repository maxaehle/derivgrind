/*--------------------------------------------------------------------*/
/*--- Client request functions.        derivgrind_clientrequests.c ---*/
/*--------------------------------------------------------------------*/

/*
   ----------------------------------------------------------------
   Notice that the following MIT license applies to this one file
   only.  The rest of Valgrind is licensed under the
   terms of the GNU General Public License, version 2, unless
   otherwise indicated.  See the COPYING file in the source
   distribution for details.
   ----------------------------------------------------------------

   This file is part of Derivgrind, an automatic differentiation
   tool applicable to compiled programs.

   Copyright (C) 2022, Chair for Scientific Computing, TU Kaiserslautern
   Copyright (C) since 2023, Chair for Scientific Computing, University of Kaiserslautern-Landau
   Homepage: https://www.scicomp.uni-kl.de
   Contact: Prof. Nicolas R. Gauger (derivgrind@projects.rptu.de)

   Lead developer: Max Aehle

   Permission is hereby granted, free of charge, to any person obtaining a copy
   of this software and associated documentation files (the "Software"), to deal
   in the Software without restriction, including without limitation the rights
   to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
   copies of the Software, and to permit persons to whom the Software is
   furnished to do so, subject to the following conditions:
   
   The above copyright notice and this permission notice shall be included in all
   copies or substantial portions of the Software.
   
   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
   IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
   FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
   AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
   LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
   OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
   SOFTWARE.

   ----------------------------------------------------------------
   Notice that the above MIT license applies to this one file
   only.  The rest of Valgrind is licensed under the
   terms of the GNU General Public License, version 2, unless
   otherwise indicated.  See the COPYING file in the source
   distribution for details.
   ----------------------------------------------------------------
*/

#include "derivgrind.h"

void dg_set_dotvalue(void** val, void** grad, int* size){
  DG_SET_DOTVALUE(*val,*grad,*size);
}
void dg_get_dotvalue(void** val, void** grad, int* size){
  DG_GET_DOTVALUE(*val,*grad,*size);
}
void dg_inputf(void** val){
  DG_INPUTF(*(unsigned long long*)*val); // actual type does not matter
}
void dg_outputf(void** val){
  DG_OUTPUTF(*(unsigned long long*)*val);
}
void dg_mark_float(void** val, int* size){
  switch(*size){
    case 4: DG_MARK_FLOAT(*(float*)*val); break;
    case 8: DG_MARK_FLOAT(*(double*)*val); break;
    case 10: DG_MARK_FLOAT(*(long double*)*val); break;
  }
}
