
/*--------------------------------------------------------------------*/
/*--- Nulgrind: The minimal Valgrind tool.               nl_main.c ---*/
/*--------------------------------------------------------------------*/

/*
   This file is part of Nulgrind, the minimal Valgrind tool,
   which does no instrumentation or analysis.

   Copyright (C) 2002-2017 Nicholas Nethercote
      njn@valgrind.org

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

#include "pub_tool_basics.h"
#include "pub_tool_tooliface.h"
#include "pub_tool_mallocfree.h"
#include "pub_tool_libcassert.h"
#include "pub_tool_gdbserver.h"
#include "pub_tool_libcbase.h"
#include "valgrind.h"


#include "shadow-memory/src/shadow.h"

inline void  shadow_free(void* addr) { VG_(free)(addr); }
inline void *shadow_malloc(size_t size) { return VG_(malloc)("Test",size); }
inline void *shadow_calloc(size_t nmemb, size_t size) { return VG_(calloc)("test", nmemb, size); }
inline void  shadow_memcpy(void* dst, void* src, size_t size) { VG_(memcpy)(dst,src,size); }
inline void  shadow_out_of_memory() {
  VG_(printf)("ERROR: Ran out of memory while allocating shadow memory.\n");
	VG_(exit)(1);
}

/*! \page storage_convention Storage convention for shadow memory
 *
 *  To each variable x, we assign a shadow variable xd of the same size
 *  and type, with the following purpose:
 *
 *  If x is a floating-point variable (float or double), xd stores the
 *  gradient of x. If x is of another type and arose from bit-wise copying
 *  of (parts of) a floating-point variable y, xd stores a copy of the
 *  respective parts of yd. Otherwise, the value of xd is unspecified.
 *
 *  In VEX IR, there are three locations where variables can exist:
 *  - For temporary variables t_i, the shadow variables are shifted
 *    temporary variables t_(i+d1) for some offset d1.
 *  - For registers r, the shadow variables are shifted registers r+d2
 *    for some offset d2.
 *  - For memory addresses p, the shadow variables are stored by the
 *    help of a *shadow memory* library. This library allows to store
 *    another byte for each byte of "original" memory.
 */

ShadowMap* my_sm = NULL; //!< Gives access to the shadow memory for the tangent variables.

static unsigned long stmt_counter = 0; //!< Can be used to tag nl_add_print_stmt outputs.

#define DEFAULT_ROUNDING IRExpr_Const(IRConst_U32(Irrm_NEAREST))

static void nl_post_clo_init(void)
{
}

/*! \page loading_and_storing Loading and storing tangent values in memory
 *
 *  When translating
 *  - an Ist_Store or Ist_StoreG statement to store data in memory, or
 *  - an Iex_Load expression or Iex_LoadG statements to load data from memory,
 *
 *  we emit Ist_Dirty statements. When those are executed, the call the
 *  functions nl_Store_diffN, nl_Load_diffN which access the shadow memory.
 *
 *  However, these functions can only accept and return 4- and 8-byte
 *  integers arguments. Therefore we reinterpret all types, including
 *  floating-point types, as 8-byte integers.
 *
 *  These casts have been implemented for Ity_I8, Ity_I16, Ity_I32, Ity_I64,
 *  Ity_F32, Ity_F64. For the remaining types, we miss suitable Iop's to
 *  convert them. As a consequence, we cannot transport gradient information
 *  through the remaining types.
 *
 */

/*! Store an 8-byte tangent value in shadow memory. Invoked by an Ist_Dirty during execution
 *  of the generated code.
 *  \param[in] addr - Address of the variable.
 *  \param[in] derivative - Tangent value, reinterpreted as 8-byte integer.
 *  \param[in] size - Original size of the variable.
 */
static
VG_REGPARM(0) void nl_Store_diff(Addr addr, ULong derivative, UChar size){
  for(int i=0; i<size; i++){
    shadow_set_bits(my_sm,((SM_Addr)addr)+i,  *( ((U8*)&derivative) + i ));
  }
}

static VG_REGPARM(0) void nl_Store_diff1(Addr addr, ULong derivative){nl_Store_diff(addr,derivative,1);}
static VG_REGPARM(0) void nl_Store_diff2(Addr addr, ULong derivative){nl_Store_diff(addr,derivative,2);}
static VG_REGPARM(0) void nl_Store_diff4(Addr addr, ULong derivative){nl_Store_diff(addr,derivative,4);}
static VG_REGPARM(0) void nl_Store_diff8(Addr addr, ULong derivative){nl_Store_diff(addr,derivative,8);}

/*! Load a tangent value from shadow memory. Invokes by an Ist_Dirty during execution
 *  of the generated code.
 *  \param[in] addr - Address of the variable.
 *  \param[in] size - Original size of the variable
 *  \returns Tangent value, reinterpreted as 8-byte integer.
 */
static
VG_REGPARM(0) ULong nl_Load_diff(Addr addr, UChar size){
  ULong derivative=0;
  for(int i=0; i<size; i++){
    shadow_get_bits(my_sm,((SM_Addr)addr)+i, (U8*)&derivative+i);
  }
  for(int i=size; i<8; i++){
    *((U8*)&derivative+i) = 0;
  }
  return derivative;
}

static VG_REGPARM(0) ULong nl_Load_diff1(Addr addr){return nl_Load_diff(addr,1);}
static VG_REGPARM(0) ULong nl_Load_diff2(Addr addr){return nl_Load_diff(addr,2);}
static VG_REGPARM(0) ULong nl_Load_diff4(Addr addr){return nl_Load_diff(addr,4);}
static VG_REGPARM(0) ULong nl_Load_diff8(Addr addr){return nl_Load_diff(addr,8);}


/*! Return whether we can reinterpret this type as integer.
 */
static Bool canConvertToI64(IRType type){
  switch(type){
    case Ity_I8: case Ity_I16: case Ity_I32: case Ity_I64:
    case Ity_F32: case Ity_F64:
      return True;
    case Ity_INVALID:
      VG_(printf)("Invalid type encountered in canConvertToI64.\n");
      tl_assert(False);
      return False;
    default:
      return False;
  }
}
/*! Reinterpret expression as 8-byte integer.
 *  Only those types are accepted for which integerIRType returns True.
 *  \param[in] expr - Expression whose value should be reinterpreted.
 *  \param[in] type - Type of expr.
 *  \returns Expression reinterpreted as 8-byte integer.
 */
static IRExpr* convertToInteger(IRExpr* expr, IRType type){
  switch(type){
    case Ity_F32:
      return IRExpr_Unop(Iop_32Uto64, IRExpr_Unop(Iop_ReinterpF32asI32, expr));
    case Ity_F64:
      return IRExpr_Unop(Iop_ReinterpF64asI64, expr);
    case Ity_I8:
      return IRExpr_Unop(Iop_8Uto64, expr);
    case Ity_I16:
      return IRExpr_Unop(Iop_16Uto64, expr);
    case Ity_I32:
      return IRExpr_Unop(Iop_32Uto64, expr);
    case Ity_I64:
      return expr;
    default:
      VG_(printf)("Bad type encountered in convertToInteger.\n");
      tl_assert(False);
      return NULL;
  }
}

/*! Reinterpret 8-byte integer expression as another type.
 *  Only those types are accepted for which integerIRType returns True.
 *  \param[in] expr - Expression whose value should be reinterpreted.
 *  \param[in] type - Type to convert to.
 *  \returns Expression reinterpreted as another type.
 */
static IRExpr* convertFromInteger(IRExpr* expr, IRType type){
  switch(type){
    case Ity_F32:
      return IRExpr_Unop(Iop_ReinterpI32asF32, expr);
    case Ity_F64:
      return IRExpr_Unop(Iop_ReinterpI64asF64, expr);
    case Ity_I8:
      return IRExpr_Unop(Iop_64to8, expr);
    case Ity_I16:
      return IRExpr_Unop(Iop_64to16, expr);
    case Ity_I32:
      return IRExpr_Unop(Iop_64to32, expr);
    case Ity_I64:
      return expr;
  }
}

/*! Print a double value during execution of the generated code, for debugging purposes.
 *  The Ist_Dirty calling this function is produced by nl_add_print_stmt.
 *  \param[in] tag - Tag that is also printed.
 *  \param[in] value - Printed value.
 */
static VG_REGPARM(0) void nl_Print_double(ULong tag, ULong value){ VG_(printf)("Value for %d : ", tag); VG_(printf)("%lf\n", *(double*)&value); }
static VG_REGPARM(0) void nl_Print_unsignedlong(ULong tag, ULong value){ VG_(printf)("Value for %d : ", tag); VG_(printf)("%p\n", (void*)value); }
static VG_REGPARM(0) void nl_Print_unsignedint(ULong tag, Int value){ VG_(printf)("Value for %d : ", tag); VG_(printf)("%p\n", (void*)value); }

/*! Debugging help. Add a dirty statement to IRSB that prints the value of expr whenever it is run.
 *  \param[in] tag - Tag of your choice, will be printed alongside.
 *  \param[in] sb_out - IRSB to which the dirty statement is added.
 *  \param[in] expr - Expression.
 */
static
void nl_add_print_stmt(ULong tag, IRSB* sb_out, IRExpr* expr){
  IRType type = typeOfIRExpr(sb_out->tyenv, expr);
  char* fname;
  void* fptr;
  IRExpr* expr_to_print;
  switch(type){
    case Ity_F64:
      fname = "nl_Print_double";
      fptr = nl_Print_double;
      expr_to_print = IRExpr_Unop(Iop_ReinterpF64asI64,expr);
      break;
    case Ity_I64:
      fname = "nl_Print_unsignedlong";
      fptr = nl_Print_unsignedlong;
      expr_to_print = expr;
      break;
    case Ity_I32:
      fname = "nl_Print_unsignedint";
      fptr = nl_Print_unsignedint;
      expr_to_print = expr;
      break;
    default:
      VG_(printf)("Bad type in nl_add_print_stmt.\n");
      return;
  }
  IRDirty* di = unsafeIRDirty_0_N(
        0,
        fname, VG_(fnptr_to_fnentry)(fptr),
        mkIRExprVec_2(IRExpr_Const(IRConst_U64(tag)), expr_to_print));
  addStmtToIRSB(sb_out, IRStmt_Dirty(di));
}

/*! React to gdb monitor commands.
 */
static
Bool nl_handle_gdb_monitor_command(ThreadId tid, HChar* req){
  HChar s[VG_(strlen)(req)+1]; //!< copy of req for strtok_r
  VG_(strcpy)(s, req);
  HChar* ssaveptr; //!< internal state of strtok_r

  const HChar commands[] = "help get set"; //!< list of possible commands
  HChar* wcmd = VG_(strtok_r)(s, " ", &ssaveptr); //!< User command
  int key = VG_(keyword_id)(commands, wcmd, kwd_report_duplicated_matches);
  switch(key){
    case -2: // multiple matches
      return True;
    case -1: // not found
      return False;
    case 0: // help
      VG_(gdb_printf)(
        "monitor commands:\n"
        "  get <addr>       - Prints derivative\n"
        "  set <addr> <val> - Sets derivative\n"
      );
      return True;
    case 1: { // get
      HChar* address_str = VG_(strtok_r)(NULL, " ", &ssaveptr);
      Addr address;
      if(!VG_(parse_Addr)(&address_str, &address)){
        VG_(gdb_printf)("Usage: get <addr>\n");
        return False;
      }
      double derivative=0;
      for(int i=0; i<8; i++){
        shadow_get_bits(my_sm,((SM_Addr)address)+i, ((U8*)&derivative)+i);
      }
      VG_(gdb_printf)("Derivative: %lf\n", derivative);
      return True;
    }
    case 2: { // set
      HChar* address_str = VG_(strtok_r)(NULL, " ", &ssaveptr);
      Addr address;
      if(!VG_(parse_Addr)(&address_str, &address)){
        VG_(gdb_printf)("Usage: set <addr> <derivative>\n");
        return False;
      }
      HChar* derivative_str = VG_(strtok_r)(NULL, " ", &ssaveptr);
      double derivative = VG_(strtod)(derivative_str, NULL);
      for(int i=0; i<8; i++){
        shadow_set_bits( my_sm,((SM_Addr)address)+i, *( ((U8*)&derivative)+i ) );
      }
      return True;
    }
    default:
      VG_(printf)("Error in nl_handle_gdb_monitor_command.\n");
      return False;
    }

}

/*! React to client requests like gdb monitor commands.
 */
static
Bool nl_handle_client_request(ThreadId tid, UWord* arg, UWord* ret){
  if(arg[0]==VG_USERREQ__GDB_MONITOR_COMMAND){
    Bool handled = nl_handle_gdb_monitor_command(tid, (HChar*)arg[1]);
    if(handled){
      *ret = 1;
    } else {
      *ret = 0;
    }
    return handled;
  } else {
    VG_(printf)("Unhandled user request.\n");
    return True;
  }
}

/*! \struct DiffEnv
 *  Data required for differentiation, is passed to differentiate_expr.
 */
typedef struct {
  /*! Shadow offset for indices of temporaries.
   */
  IRTemp t_offset;
  /*! Layout argument to nl_instrument.
   *  layout->total_sizeB is the shadow offset for register indices. */
  const VexGuestLayout* layout;
  /*! Add helper statements to this IRSB.
   */
  IRSB* sb_out;
} DiffEnv;

/*! Differentiate an expression.
 *
 *  - For arithmetic expressions involving float or double variables, we
 *    use the respective differentiation rules. Not all kinds of operations
 *    have been implemented already.
 *  - For expressions that just copy bytes, we copy the respective shadow
 *    bytes.
 *  - Otherwise, we return NULL. The function differentiate_or_zero has
 *    been created to return zero bytes and possibly output a warning.
 *
 *  The function might add helper statements to diffenv.sb_out.
 *
 *  \param[in] ex - Expression.
 *  \param[in,out] diffenv - Additional data necessary for differentiation.
 *  \returns Differentiated expression or NULL.
 */
static
IRExpr* differentiate_expr(IRExpr const* ex, DiffEnv diffenv ){
  if(ex->tag==Iex_Triop){
    IRTriop* rex = ex->Iex.Triop.details;
    IRExpr* arg1 = rex->arg1;
    IRExpr* arg2 = rex->arg2;
    IRExpr* arg3 = rex->arg3;
    IRExpr* d2 = differentiate_expr(arg2,diffenv);
    IRExpr* d3 = differentiate_expr(arg3,diffenv);
    if(d2==NULL || d3==NULL) return NULL;
    switch(rex->op){
      case Iop_AddF64: return IRExpr_Triop(Iop_AddF64,arg1,d2,d3);
      case Iop_AddF32: return IRExpr_Triop(Iop_AddF32,arg1,d2,d3);
      case Iop_SubF64: return IRExpr_Triop(Iop_SubF64,arg1,d2,d3);
      case Iop_SubF32: return IRExpr_Triop(Iop_SubF32,arg1,d2,d3);
      case Iop_MulF64:
        return IRExpr_Triop(Iop_AddF64,arg1,
          IRExpr_Triop(Iop_MulF64, arg1, d2,arg3),
          IRExpr_Triop(Iop_MulF64, arg1, d3,arg2)
        );
      case Iop_MulF32:
        return IRExpr_Triop(Iop_AddF32,arg1,
          IRExpr_Triop(Iop_MulF32, arg1, d2,arg3),
          IRExpr_Triop(Iop_MulF32, arg1, d3,arg2)
        );
      case Iop_DivF64:
        return IRExpr_Triop(Iop_DivF64,arg1,
          IRExpr_Triop(Iop_SubF64, arg1,
            IRExpr_Triop(Iop_MulF64, arg1, d2,arg3),
            IRExpr_Triop(Iop_MulF64, arg1, d3,arg2)
          ),
          IRExpr_Triop(Iop_MulF64, arg1, arg3, arg3)
        );
      case Iop_DivF32:
        return IRExpr_Triop(Iop_DivF32,arg1,
          IRExpr_Triop(Iop_SubF32, arg1,
            IRExpr_Triop(Iop_MulF32, arg1, d2,arg3),
            IRExpr_Triop(Iop_MulF32, arg1, d3,arg2)
          ),
          IRExpr_Triop(Iop_MulF32, arg1, arg3, arg3)
        );
      default:
        return NULL;
    }
  } else if(ex->tag==Iex_Binop) {
    IROp op = ex->Iex.Binop.op;
    IRExpr* arg1 = ex->Iex.Binop.arg1;
    IRExpr* arg2 = ex->Iex.Binop.arg2;
    IRExpr* d2 = differentiate_expr(arg2,diffenv);
    if(d2==NULL) return NULL;
    switch(op){
      case Iop_SqrtF64: {
        IRExpr* numerator = d2;
        IRExpr* consttwo = IRExpr_Const(IRConst_F64(2.0));
        IRExpr* denominator =  IRExpr_Triop(Iop_MulF64, arg1, consttwo, IRExpr_Binop(Iop_SqrtF64, arg1, arg2) );
        return IRExpr_Triop(Iop_DivF64, arg1, numerator, denominator);
      }
      case Iop_SqrtF32: {
        IRExpr* numerator = d2;
        IRExpr* consttwo = IRExpr_Const(IRConst_F32(2.0));
        IRExpr* denominator =  IRExpr_Triop(Iop_MulF32, arg1, consttwo, IRExpr_Binop(Iop_SqrtF32, arg1, arg2) );
        return IRExpr_Triop(Iop_DivF32, arg1, numerator, denominator);
      }
      default:
        return NULL;
    }
  } else if(ex->tag==Iex_Unop) {
    IROp op = ex->Iex.Unop.op;
    IRExpr* arg = ex->Iex.Unop.arg;
    IRExpr* d = differentiate_expr(arg,diffenv);
    if(d==NULL) return NULL;
    switch(op){
      case Iop_NegF64: return IRExpr_Unop(Iop_NegF64,d);
      case Iop_NegF32: return IRExpr_Unop(Iop_NegF32,d);
      case Iop_AbsF64: {
        // If arg >= 0, we get Ircr_GT or Ircr_EQ, thus the Iop_32to1 gives a 0 bit.
        IRExpr* cond = IRExpr_Binop(Iop_CmpF64, arg, IRExpr_Const(IRConst_F64(0.)));
        IRExpr* minus_d = IRExpr_Unop(Iop_NegF64, d);
        return IRExpr_ITE(IRExpr_Unop(Iop_32to1,cond), minus_d, d);
      }
      case Iop_AbsF32: {
        IRExpr* cond = IRExpr_Binop(Iop_CmpF32, arg, IRExpr_Const(IRConst_F32(0.)));
        IRExpr* minus_d = IRExpr_Unop(Iop_NegF32, d);
        return IRExpr_ITE(IRExpr_Unop(Iop_32to1,cond), minus_d, d);
      }
      default:
        return NULL;
    }
  } else if(ex->tag==Iex_Const) {
    IRConstTag type = ex->Iex.Const.con->tag;
    switch(type){
      case Ico_F64: return IRExpr_Const(IRConst_F64(0.));
      case Ico_F64i: return IRExpr_Const(IRConst_F64i(0));    // TODO why are these not Ity_F64 etc. types?
      case Ico_F32: return IRExpr_Const(IRConst_F32(0.));
      case Ico_F32i: return IRExpr_Const(IRConst_F32i(0));
      case Ico_U1: return IRExpr_Const(IRConst_U1(0));
      case Ico_U8: return IRExpr_Const(IRConst_U8(0));
      case Ico_U16: return IRExpr_Const(IRConst_U16(0));
      case Ico_U32: return IRExpr_Const(IRConst_U32(0));
      case Ico_U64: return IRExpr_Const(IRConst_U64(0));
      case Ico_U128: return IRExpr_Const(IRConst_U128(0));
      case Ico_V128: return IRExpr_Const(IRConst_V128(0));
      case Ico_V256: return IRExpr_Const(IRConst_V256(0));
      default: tl_assert(False); return NULL;
    }
  } else if(ex->tag==Iex_ITE) {
    IRExpr* dtrue = differentiate_expr(ex->Iex.ITE.iftrue, diffenv);
    IRExpr* dfalse = differentiate_expr(ex->Iex.ITE.iffalse, diffenv);
    VG_(printf)("generate ITE: iftrue=\n");
    ppIRExpr(dtrue);
    VG_(printf)("\niffalse=\n");
    ppIRExpr(dfalse);
    VG_(printf)("\n");
    if(dtrue==NULL || dfalse==NULL) return NULL;
    else return IRExpr_ITE(ex->Iex.ITE.cond, dtrue, dfalse);
  } else if(ex->tag==Iex_RdTmp) {
    return IRExpr_RdTmp( ex->Iex.RdTmp.tmp + diffenv.t_offset );
  } else if(ex->tag==Iex_Get) {
    return IRExpr_Get(ex->Iex.Get.offset+diffenv.layout->total_sizeB,ex->Iex.Get.ty);
  } else if(ex->tag==Iex_GetI) {
    IRRegArray* descr = ex->Iex.GetI.descr;
    IRExpr* ix = ex->Iex.GetI.ix;
    Int bias = ex->Iex.GetI.bias;
    IRRegArray* descr_diff = mkIRRegArray(descr->base+diffenv.layout->total_sizeB,descr->elemTy,descr->nElems);
    return IRExpr_GetI(descr_diff,ix,bias+diffenv.layout->total_sizeB);
  } else if (ex->tag==Iex_Load) {
      // TODO treat D16,D32,D64,V256,F16
    // Load tangent value into this temporary.
    // Apparantly it must be of an integer type.
    IRType type = ex->Iex.Load.ty;
    if(!canConvertToI64(type)) return NULL;
    IRTemp loadAddr = newIRTemp(diffenv.sb_out->tyenv, Ity_I64);
    const char* fname;
    void* fn;
    switch(sizeofIRType(type)){
      case 1: fname="nl_Load_diff1"; fn=nl_Load_diff1; break;
      case 2: fname="nl_Load_diff2"; fn=nl_Load_diff2; break;
      case 4: fname="nl_Load_diff4"; fn=nl_Load_diff4; break;
      case 8: fname="nl_Load_diff8"; fn=nl_Load_diff8; break;
      default: tl_assert(False);
    }
    IRDirty* di = unsafeIRDirty_1_N(
      loadAddr,
      0,
      fname, VG_(fnptr_to_fnentry)(fn),
      mkIRExprVec_1(ex->Iex.Load.addr));
    addStmtToIRSB(diffenv.sb_out, IRStmt_Dirty(di));
    // Convert into correct type.
    IRTemp loadAddr_reinterpreted = newIRTemp(diffenv.sb_out->tyenv, type);
    addStmtToIRSB(diffenv.sb_out, IRStmt_WrTmp(loadAddr_reinterpreted,
      convertFromInteger(IRExpr_RdTmp(loadAddr), type)));
    return IRExpr_RdTmp(loadAddr_reinterpreted);
  } else {
    return NULL;
  }
}

/*! Checks if diff is not NULL, and possibly outputs warning.
 *  \param[in] expr - expression to be differentiated
 *  \param[in] diffenv - Additional data necessary for differentiation.
 *  \param[in] warn - If true, a warning message will be printed if derivative is not found.
 *  \param[in] operation - Word for how the derivative is used, e.g. 'WrTmp' or 'Store'.
 * \returns Differentiated expression or NULL
 */
static
IRExpr* differentiate_or_warn(IRExpr* expr, DiffEnv diffenv, Bool warn, const char* operation){
  IRExpr* diff = differentiate_expr(expr,diffenv);
  if(diff==NULL && warn){
    VG_(printf)("Warning: Expression\n");
    ppIRExpr(expr);
    VG_(printf)("could not be differentiated in %s.\n\n", operation);
  }
  return diff;
}

/*! Instrument an IRSB.
 */
static
IRSB* nl_instrument ( VgCallbackClosure* closure,
                      IRSB* sb_in,
                      const VexGuestLayout* layout, 
                      const VexGuestExtents* vge,
                      const VexArchInfo* archinfo_host,
                      IRType gWordTy, IRType hWordTy )
{
  int i;
  DiffEnv diffenv;
  IRSB* sb_out = deepCopyIRSBExceptStmts(sb_in);
  // append the "gradient temporaries" to the "value temporaries",
  // doubling the number of temporaries
  diffenv.t_offset = sb_in->tyenv->types_used;
  for(IRTemp t=0; t<diffenv.t_offset; t++){
    newIRTemp(sb_out->tyenv, sb_in->tyenv->types[t]);
  }

  diffenv.sb_out = sb_out;
  diffenv.layout = layout;

  // copy until IMark
  i = 0;
  while (i < sb_in->stmts_used && sb_in->stmts[i]->tag != Ist_IMark) {
     addStmtToIRSB(sb_out, sb_in->stmts[i]);
     i++;
  }
  for (/* use current i*/; i < sb_in->stmts_used; i++) {
    stmt_counter++;
    IRStmt* st_orig = sb_in->stmts[i]; // will be added to sb_out in the end
    const IRStmt* st = st_orig; // const version for differentiation
    //VG_(printf)("next stmt %d :",stmt_counter); ppIRStmt(st); VG_(printf)("\n");
    if(st->tag==Ist_WrTmp) {
      IRType type = sb_in->tyenv->types[st->Ist.WrTmp.tmp];
      IRExpr* differentiated_expr = differentiate_or_warn(st->Ist.WrTmp.data, diffenv,type==Ity_F64||type==Ity_F32,"WrTmp");
      if(differentiated_expr){
        IRStmt* sp = IRStmt_WrTmp(st->Ist.WrTmp.tmp+diffenv.t_offset, differentiated_expr);
        addStmtToIRSB(sb_out, sp);
      }
    } else if(st->tag==Ist_Put) {
      IRType type = typeOfIRExpr(sb_in->tyenv,st->Ist.Put.data);
      IRExpr* differentiated_expr = differentiate_or_warn(st->Ist.Put.data, diffenv,type==Ity_F64||type==Ity_F32,"Put");
      if(differentiated_expr){
        IRStmt* sp = IRStmt_Put(st->Ist.Put.offset + diffenv.layout->total_sizeB, differentiated_expr);
        addStmtToIRSB(sb_out, sp);
      }
    } else if(st->tag==Ist_PutI) {
      IRType type = typeOfIRExpr(sb_in->tyenv,st->Ist.PutI.details->data);
      IRExpr* differentiated_expr = differentiate_or_warn(st->Ist.PutI.details->data, diffenv,type==Ity_F64||type==Ity_F32,"PutI");
      if(differentiated_expr){
        IRRegArray* descr = st->Ist.PutI.details->descr;
        IRRegArray* descr_diff = mkIRRegArray(descr->base+diffenv.layout->total_sizeB, descr->elemTy, descr->nElems);
        Int bias = st->Ist.PutI.details->bias;
        IRExpr* ix = st->Ist.PutI.details->ix;
        IRStmt* sp = IRStmt_PutI(mkIRPutI(descr_diff,ix,bias+diffenv.layout->total_sizeB,differentiated_expr));
        addStmtToIRSB(sb_out, sp);
      }
    } else if(st->tag==Ist_Store || st->tag==Ist_StoreG) {
      IREndness end; IRExpr* addr; IRExpr* data; Bool guarded;
      if(st->tag == Ist_Store){
        end = st->Ist.Store.end;
        addr = st->Ist.Store.addr;
        data = st->Ist.Store.data;
        guarded = False;
      } else {
        end = st->Ist.StoreG.details->end;
        addr = st->Ist.StoreG.details->addr;
        data = st->Ist.StoreG.details->data;
        guarded = True;
      }
      IRType type = typeOfIRExpr(sb_in->tyenv,data);
      if(canConvertToI64(type)){
        IRExpr* differentiated_expr = differentiate_or_warn(data, diffenv, type==Ity_F64||type==Ity_F32,"Store");
        if(differentiated_expr){
          // The Store.data is an IREXpr_Const or IRExpr_Tmp, so this holds
          // for its derivative as well. Compare this to Memcheck's IRAtom.
          // TODO treat D16,D32,D64,V256,F16
          // Before we can store the tangent value, we must convert
          // it to an integer type. Compare to Iex_Load.
          IRExpr* differentiated_expr_reinterpreted = convertToInteger(differentiated_expr,type);
          const char* fname;
          void* fn;
          switch(sizeofIRType(type)){
            case 1: fname="nl_Store_diff1"; fn=nl_Store_diff1; break;
            case 2: fname="nl_Store_diff2"; fn=nl_Store_diff2; break;
            case 4: fname="nl_Store_diff4"; fn=nl_Store_diff4; break;
            case 8: fname="nl_Store_diff8"; fn=nl_Store_diff8; break;
            default: tl_assert(False);
          }
          VG_(printf)("Adding dirty: "); ppIRExpr(differentiated_expr_reinterpreted); VG_(printf)("\n");
          IRDirty* di = unsafeIRDirty_0_N(
                  0,
                  fname, VG_(fnptr_to_fnentry)(fn),
                  mkIRExprVec_2(addr,differentiated_expr_reinterpreted));
          if(guarded){
            di->guard = st->Ist.StoreG.details->guard;
          }
          IRStmt* sp = IRStmt_Dirty(di);
          addStmtToIRSB(sb_out, sp);
        }
      }
    } else if(st->tag==Ist_LoadG) {
      IRLoadG* det = st->Ist.LoadG.details;
      IRType type = sb_in->tyenv->types[det->dst];
      if(canConvertToI64(type)){
        if(type==Ity_F64) tl_assert(det->cvt == ILGop_Ident64); // what else could you load into double?
        if(type==Ity_F32) tl_assert(det->cvt == ILGop_Ident32); // what else could you load into double?
        IRExpr* differentiated_expr_alt = differentiate_or_warn(det->alt,diffenv,type==Ity_F64||type==Ity_F32,"alternative-LoadG");
        if(differentiated_expr_alt){
          // compare the following to Iop_Load
          IRTemp loadAddr = newIRTemp(diffenv.sb_out->tyenv, Ity_I64);
          const char* fname;
          void* fn;
          switch(sizeofIRType(type)){
            case 1: fname="nl_Load_diff1"; fn=nl_Load_diff1; break;
            case 2: fname="nl_Load_diff2"; fn=nl_Load_diff2; break;
            case 4: fname="nl_Load_diff4"; fn=nl_Load_diff4; break;
            case 8: fname="nl_Load_diff8"; fn=nl_Load_diff8; break;
            default: tl_assert(False);
          }
          IRDirty* di = unsafeIRDirty_1_N(
                loadAddr,
                0,
                fname, VG_(fnptr_to_fnentry)(fn),
                mkIRExprVec_1(det->addr));
          addStmtToIRSB(diffenv.sb_out, IRStmt_Dirty(di));
          IRTemp loadAddr_reinterpreted = newIRTemp(diffenv.sb_out->tyenv, type);
          addStmtToIRSB(diffenv.sb_out, IRStmt_WrTmp(loadAddr_reinterpreted,
              convertFromInteger(IRExpr_RdTmp(loadAddr), type)));
          // copy it into shadow variable for temporary dst,
          // or the derivative of alt, depending on the guard
          addStmtToIRSB(diffenv.sb_out, IRStmt_WrTmp(det->dst+diffenv.t_offset,
            IRExpr_ITE(det->guard,IRExpr_RdTmp(loadAddr_reinterpreted),differentiated_expr_alt)
          ));
        }
      }
    } else if(st->tag==Ist_CAS) {
      VG_(printf)("Did not instrument Ist_CAS statement.\n");
      IRTemp oldHi = st->Ist.CAS.details->oldHi;
      IRTemp oldLo = st->Ist.CAS.details->oldLo;
      addStmtToIRSB(diffenv.sb_out, IRStmt_WrTmp(oldLo + diffenv.t_offset, IRExpr_Const(IRConst_F64(0.))));
      if(oldHi != IRTemp_INVALID)
        addStmtToIRSB(diffenv.sb_out, IRStmt_WrTmp(oldHi + diffenv.t_offset, IRExpr_Const(IRConst_F64(0.))));
    } else if(st->tag==Ist_LLSC) {
      VG_(printf)("Did not instrument Ist_LLSC statement.\n");
    } else if(st->tag==Ist_Dirty) {
      VG_(printf)("Cannot instrument Ist_Dirty statement.\n");
    } else if(st->tag==Ist_NoOp || st->tag==Ist_IMark || st->tag==Ist_AbiHint){
      // no relevance for any tool, do nothing
    } else if(st->tag==Ist_Exit || st->tag==Ist_MBE) {
      // no relevance for AD, do nothing
    } else {
      tl_assert(False);
    }
    addStmtToIRSB(sb_out, st_orig);

  }

   return sb_out;
}

static void nl_fini(Int exitcode)
{
  shadow_destroy_map(my_sm);
  VG_(free)(my_sm);
}

static void nl_pre_clo_init(void)
{


   VG_(details_name)            ("Nulgrind");
   VG_(details_version)         (NULL);
   VG_(details_description)     ("the minimal Valgrind tool");
   VG_(details_copyright_author)(
      "Copyright (C) 2002-2017, and GNU GPL'd, by Nicholas Nethercote.");
   VG_(details_bug_reports_to)  (VG_BUGS_TO);

   VG_(details_avg_translation_sizeB) ( 275 );

   VG_(basic_tool_funcs)        (nl_post_clo_init,
                                 nl_instrument,
                                 nl_fini);


   /* No needs, no core events to track */
   VG_(printf)("Allocate SM...");
   my_sm = (ShadowMap*) VG_(malloc)("Some text", sizeof(ShadowMap));
   if(my_sm==NULL) VG_(printf)("Error\n");
   my_sm->shadow_bits = 1;
   my_sm->application_bits = 1;
   my_sm->num_distinguished = 1;
   shadow_initialize_map(my_sm);
   VG_(printf)("done\n");

   VG_(needs_client_requests)     (nl_handle_client_request);

  shadow_set_bits(my_sm, 0xffff1111, 0xab); // spam

}

VG_DETERMINE_INTERFACE_VERSION(nl_pre_clo_init)

/*--------------------------------------------------------------------*/
/*--- end                                                          ---*/
/*--------------------------------------------------------------------*/
