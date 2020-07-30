/*
 * Copyright 2020 Red Hat Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */
#include "codegen/nv50_ir_target_gv100.h"
#include "codegen/nv50_ir_lowering_gv100.h"
#include "codegen/nv50_ir_emit_gv100.h"

namespace nv50_ir {

void
TargetGV100::initOpInfo()
{
   unsigned int i, j;

   static const operation commutative[] =
   {
      OP_ADD, OP_MUL, OP_MAD, OP_FMA, OP_MAX, OP_MIN,
      OP_SET_AND, OP_SET_OR, OP_SET_XOR, OP_SET, OP_SELP, OP_SLCT
   };

   static const operation noDest[] =
   {
      OP_EXIT
   };

   static const operation noPred[] =
   {
   };

   for (i = 0; i < DATA_FILE_COUNT; ++i)
      nativeFileMap[i] = (DataFile)i;
   nativeFileMap[FILE_ADDRESS] = FILE_GPR;
   nativeFileMap[FILE_FLAGS] = FILE_PREDICATE;

   for (i = 0; i < OP_LAST; ++i) {
      opInfo[i].variants = NULL;
      opInfo[i].op = (operation)i;
      opInfo[i].srcTypes = 1 << (int)TYPE_F32;
      opInfo[i].dstTypes = 1 << (int)TYPE_F32;
      opInfo[i].immdBits = 0;
      opInfo[i].srcNr = operationSrcNr[i];

      for (j = 0; j < opInfo[i].srcNr; ++j) {
         opInfo[i].srcMods[j] = 0;
         opInfo[i].srcFiles[j] = 1 << (int)FILE_GPR;
      }
      opInfo[i].dstMods = 0;
      opInfo[i].dstFiles = 1 << (int)FILE_GPR;

      opInfo[i].hasDest = 1;
      opInfo[i].vector = (i >= OP_TEX && i <= OP_TEXCSAA);
      opInfo[i].commutative = false; /* set below */
      opInfo[i].pseudo = (i < OP_MOV);
      opInfo[i].predicate = !opInfo[i].pseudo;
      opInfo[i].flow = (i >= OP_BRA && i <= OP_JOIN);
      opInfo[i].minEncSize = 16;
   }
   for (i = 0; i < ARRAY_SIZE(commutative); ++i)
      opInfo[commutative[i]].commutative = true;
   for (i = 0; i < ARRAY_SIZE(noDest); ++i)
      opInfo[noDest[i]].hasDest = 0;
   for (i = 0; i < ARRAY_SIZE(noPred); ++i)
      opInfo[noPred[i]].predicate = 0;
}

struct opInfo {
   uint16_t def;
   struct {
      uint16_t files;
      uint8_t mods;
   } src[3];
};

#define SRC_NONE 0
#define SRC_U    (1 << FILE_UGPR)
#define SRC_R    (1 << FILE_GPR)
#define SRC_I    (1 << FILE_MEMORY_CONST)
#define SRC_C    (1 << FILE_IMMEDIATE)
#define SRC_P    (1 << FILE_PREDICATE)
#define SRC_UR   (SRC_R |                 SRC_U)
#define SRC_RC   (SRC_R |         SRC_C        )
#define SRC_RI   (SRC_R | SRC_I                )
#define SRC_URI  (SRC_R | SRC_I |         SRC_U)
#define SRC_RIC  (SRC_R | SRC_I | SRC_C        )
#define SRC_URIC (SRC_R | SRC_I | SRC_C | SRC_U)

#define MOD_NONE 0
#define MOD_NEG  NV50_IR_MOD_NEG
#define MOD_ABS  NV50_IR_MOD_ABS
#define MOD_NOT  NV50_IR_MOD_NOT
#define MOD_NA   (MOD_NEG | MOD_ABS)

#define OPINFO(O,DA,SA,MA,SB,MB,SC,MC)                                         \
static struct opInfo                                                           \
opInfo_##O = {                                                                 \
   .def = SRC_##DA,                                                            \
   .src = { { SRC_##SA, MOD_##MA },                                            \
            { SRC_##SB, MOD_##MB },                                            \
            { SRC_##SC, MOD_##MC }},                                           \
};


/* Handled by GV100LegalizeSSA. */
OPINFO(FABS     , R   , URIC, NA  , NONE, NONE, NONE, NONE);
OPINFO(FCMP     , R   , R   , NONE, URIC, NONE, URIC, NONE); //XXX: use FSEL for mods
OPINFO(FNEG     , R   , URIC, NA  , NONE, NONE, NONE, NONE);
OPINFO(FSET     , R   , R   , NA  , URIC, NA  , NONE, NONE);
OPINFO(ICMP     , R   , R   , NONE, URIC, NONE, URIC, NONE);
OPINFO(IMUL     , R   , R   , NONE, URIC, NONE, NONE, NONE);
OPINFO(INEG     , R   , URIC, NEG , NONE, NONE, NONE, NONE);
OPINFO(ISET     , R   , R   , NONE, URIC, NONE, NONE, NONE);
OPINFO(LOP2     , R   , R   , NOT , URIC, NOT , NONE, NONE);
OPINFO(NOT      , R   , URIC, NONE, NONE, NONE, NONE, NONE);
OPINFO(SAT      , R   , URIC, NA  , NONE, NONE, NONE, NONE);
OPINFO(SHL      , R   , URIC, NONE, URIC, NONE, NONE, NONE);
OPINFO(SHR      , R   , URIC, NONE, URIC, NONE, NONE, NONE);
OPINFO(SUB      , R   , R   , NONE, URIC, NEG , NONE, NONE);
OPINFO(IMNMX    , R   , R   , NONE, URIC, NONE, NONE, NONE);

/* Handled by CodeEmitterGV100. */
OPINFO(AL2P     , R   , NONE, NONE, NONE, NONE, NONE, NONE);
OPINFO(ALD      , R   , NONE, NONE, NONE, NONE, NONE, NONE);
OPINFO(AST      , NONE, NONE, NONE, NONE, NONE, NONE, NONE);
OPINFO(ATOM     , R   , NONE, NONE, NONE, NONE, NONE, NONE);
OPINFO(ATOMS    , R   , NONE, NONE, NONE, NONE, NONE, NONE);
OPINFO(BAR      , NONE, NONE, NONE, NONE, NONE, NONE, NONE);
OPINFO(BRA      , NONE, NONE, NONE, NONE, NONE, NONE, NONE);
OPINFO(BMSK     , UR  , R   , NONE, URIC, NONE, NONE, NONE);
OPINFO(BREV     , UR  , URIC, NONE, NONE, NONE, NONE, NONE);
OPINFO(CCTL     , NONE, NONE, NONE, NONE, NONE, NONE, NONE);
//OPINFO(CS2R     , R   , NONE, NONE, NONE, NONE, NONE, NONE);
OPINFO(DADD     , R   , R   , NA  , URIC, NA  , NONE, NONE);
OPINFO(DFMA     , R   , R   , NA  , URIC, NA  , URIC, NA  );
OPINFO(DMUL     , R   , R   , NA  , URIC, NA  , NONE, NONE);
OPINFO(DSETP    , R   , R   , NA  , URIC, NA  , P   , NONE);
OPINFO(EXIT     , NONE, NONE, NONE, NONE, NONE, NONE, NONE);
OPINFO(F2F      , R   , URIC, NA  , NONE, NONE, NONE, NONE);
OPINFO(F2I      , R   , URIC, NA  , NONE, NONE, NONE, NONE);
OPINFO(FADD     , R   , R   , NA  , URIC, NA  , NONE, NONE);
OPINFO(FFMA     , R   , R   , NA  , URIC, NA  , URIC, NA  );
OPINFO(FLO      , UR  , URIC, NOT , NONE, NONE, NONE, NONE);
OPINFO(FMNMX    , R   , R   , NA  , URIC, NA  , NONE, NONE);
OPINFO(FMUL     , R   , R   , NA  , URIC, NA  , NONE, NONE);
OPINFO(FRND     , R   , URIC, NA  , NONE, NONE, NONE, NONE);
OPINFO(FSET_BF  , R   , R   , NA  , URIC, NA  , NONE, NONE);
OPINFO(FSETP    , R   , R   , NA  , URIC, NA  , P   , NONE);
OPINFO(FSWZADD  , R   , R   , NONE, R   , NONE, NONE, NONE);
OPINFO(I2F      , R   , URIC, NONE, NONE, NONE, NONE, NONE);
OPINFO(IABS     , R   , URIC, NONE, NONE, NONE, NONE, NONE);
OPINFO(IADD3    , UR  , R   , NEG , URIC, NEG , R   , NEG );
OPINFO(IMAD     , UR  , R   , NONE, URIC, NONE, URIC, NEG );
OPINFO(IMAD_WIDE, UR  , R   , NONE, URIC, NONE, RC  , NEG );
OPINFO(IPA      , R   , NONE, NONE, NONE, NONE, NONE, NONE);
OPINFO(ISBERD   , R   , NONE, NONE, NONE, NONE, NONE, NONE);
OPINFO(ISETP    , P   , R   , NONE, URIC, NONE, P   , NONE);
OPINFO(KILL     , NONE, NONE, NONE, NONE, NONE, NONE, NONE);
OPINFO(LD       , R   , NONE, NONE, NONE, NONE, NONE, NONE);
OPINFO(LDC      , UR  , C   , NONE, NONE, NONE, NONE, NONE);
OPINFO(LDL      , R   , NONE, NONE, NONE, NONE, NONE, NONE);
OPINFO(LDS      , R   , NONE, NONE, NONE, NONE, NONE, NONE);
OPINFO(LEA      , UR  , R   , NEG , I   , NONE, URIC, NEG );
OPINFO(LOP3_LUT , UR  , R   , NONE, URIC, NONE, R   , NONE);
OPINFO(MEMBAR   , NONE, NONE, NONE, NONE, NONE, NONE, NONE);
OPINFO(MOV      , UR  , URIC, NONE, NONE, NONE, NONE, NONE);
OPINFO(MUFU     , R   , URIC, NA  , NONE, NONE, NONE, NONE);
OPINFO(NOP      , NONE, NONE, NONE, NONE, NONE, NONE, NONE);
OPINFO(OUT      , R   , R   , NONE, URI , NONE, NONE, NONE);
OPINFO(PIXLD    , R   , NONE, NONE, NONE, NONE, NONE, NONE);
OPINFO(PLOP3_LUT, UR  , NONE, NONE, NONE, NONE, NONE, NONE);
OPINFO(POPC     , UR  , URIC, NOT , NONE, NONE, NONE, NONE);
OPINFO(PRMT     , UR  , R   , NONE, URIC, NONE, URIC, NONE);
OPINFO(RED      , R   , NONE, NONE, NONE, NONE, NONE, NONE);
OPINFO(SGXT     , UR  , R   , NONE, URIC, NONE, NONE, NONE);
OPINFO(S2R      , R   , NONE, NONE, NONE, NONE, NONE, NONE);
OPINFO(SEL      , UR  , R   , NONE, URIC, NONE, NONE, NONE);
OPINFO(SHF      , UR  , R   , NONE, URIC, NONE, URIC, NONE);
OPINFO(SHFL     , R   , R   , NONE, R   , NONE, R   , NONE);
OPINFO(ST       , NONE, NONE, NONE, NONE, NONE, NONE, NONE);
OPINFO(STL      , NONE, NONE, NONE, NONE, NONE, NONE, NONE);
OPINFO(STS      , NONE, NONE, NONE, NONE, NONE, NONE, NONE);
OPINFO(SUATOM   , R   , NONE, NONE, NONE, NONE, NONE, NONE);
OPINFO(SULD     , R   , NONE, NONE, NONE, NONE, NONE, NONE);
OPINFO(SUST     , NONE, NONE, NONE, NONE, NONE, NONE, NONE);
OPINFO(TEX      , R   , NONE, NONE, NONE, NONE, NONE, NONE);
OPINFO(TLD      , R   , NONE, NONE, NONE, NONE, NONE, NONE);
OPINFO(TLD4     , R   , NONE, NONE, NONE, NONE, NONE, NONE);
OPINFO(TMML     , R   , NONE, NONE, NONE, NONE, NONE, NONE);
OPINFO(TXD      , R   , NONE, NONE, NONE, NONE, NONE, NONE);
OPINFO(TXQ      , R   , NONE, NONE, NONE, NONE, NONE, NONE);
OPINFO(VOTE     , UR  , NONE, NONE, NONE, NONE, NONE, NONE);
OPINFO(WARPSYNC , NONE, R   , NONE, NONE, NONE, NONE, NONE);

static const struct opInfo *
getOpInfo(const Instruction *i)
{
   switch (i->op) {
   case OP_ABS:
      if (isFloatType(i->dType))
         return &opInfo_FABS;
      return &opInfo_IABS;
   case OP_ADD:
      if (isFloatType(i->dType)) {
         if (i->dType == TYPE_F32)
            return &opInfo_FADD;
         else
            return &opInfo_DADD;
      } else {
         return &opInfo_IADD3;
      }
      break;
   case OP_AFETCH: return &opInfo_AL2P;
   case OP_AND:
   case OP_OR:
   case OP_XOR:
      if (i->def(0).getFile() == FILE_PREDICATE)
         return &opInfo_PLOP3_LUT;
      return &opInfo_LOP2;
   case OP_ATOM:
      if (i->src(0).getFile() == FILE_MEMORY_SHARED)
         return &opInfo_ATOMS;
      else
         if (!i->defExists(0) && i->subOp < NV50_IR_SUBOP_ATOM_CAS)
            return &opInfo_RED;
         else
            return &opInfo_ATOM;
      break;
   case OP_BAR: return &opInfo_BAR;
   case OP_BFIND: return &opInfo_FLO;
   case OP_BMSK: return &opInfo_BMSK;
   case OP_BREV: return &opInfo_BREV;
   case OP_BRA:
   case OP_JOIN: return &opInfo_BRA; //XXX
   case OP_CCTL: return &opInfo_CCTL;
   case OP_CEIL:
   case OP_CVT:
   case OP_FLOOR:
   case OP_TRUNC:
      if (i->op == OP_CVT && (i->def(0).getFile() == FILE_PREDICATE ||
                                 i->src(0).getFile() == FILE_PREDICATE)) {
         return &opInfo_MOV;
      } else if (isFloatType(i->dType)) {
         if (isFloatType(i->sType)) {
            if (i->sType == i->dType)
               return &opInfo_FRND;
            else
               return &opInfo_F2F;
         } else {
            return &opInfo_I2F;
         }
      } else {
         if (isFloatType(i->sType))
            return &opInfo_F2I;
      }
      break;
   case OP_COS:
   case OP_EX2:
   case OP_LG2:
   case OP_RCP:
   case OP_RSQ:
   case OP_SIN:
   case OP_SQRT: return &opInfo_MUFU;
   case OP_DISCARD: return &opInfo_KILL;
   case OP_EMIT:
   case OP_FINAL:
   case OP_RESTART: return &opInfo_OUT;
   case OP_EXIT: return &opInfo_EXIT;
   case OP_EXPORT: return &opInfo_AST;
   case OP_FMA:
   case OP_MAD:
      if (isFloatType(i->dType)) {
         if (i->dType == TYPE_F32)
            return &opInfo_FFMA;
         else
            return &opInfo_DFMA;
      } else {
         if (typeSizeof(i->dType) != 8)
            return &opInfo_IMAD;
         else
            return &opInfo_IMAD_WIDE;
      }
      break;
   case OP_JOINAT: return &opInfo_NOP; //XXX
   case OP_LINTERP: return &opInfo_IPA;
   case OP_LOAD:
      switch (i->src(0).getFile()) {
      case FILE_MEMORY_CONST : return &opInfo_LDC;
      case FILE_MEMORY_LOCAL : return &opInfo_LDL;
      case FILE_MEMORY_SHARED: return &opInfo_LDS;
      case FILE_MEMORY_GLOBAL: return &opInfo_LD;
      default:
         break;
      }
      break;
   case OP_LOP3_LUT: return &opInfo_LOP3_LUT;
   case OP_MAX:
   case OP_MIN:
      if (isFloatType(i->dType)) {
         if (i->dType == TYPE_F32)
            return &opInfo_FMNMX;
      } else {
         return &opInfo_IMNMX;
      }
      break;
   case OP_MEMBAR: return &opInfo_MEMBAR;
   case OP_MOV: return &opInfo_MOV;
   case OP_MUL:
      if (isFloatType(i->dType)) {
         if (i->dType == TYPE_F32)
            return &opInfo_FMUL;
         else
            return &opInfo_DMUL;
      }
      return &opInfo_IMUL;
   case OP_NEG:
      if (isFloatType(i->dType))
         return &opInfo_FNEG;
      return &opInfo_INEG;
   case OP_NOT: return &opInfo_NOT;
   case OP_PERMT: return &opInfo_PRMT;
   case OP_PFETCH: return &opInfo_ISBERD;
   case OP_PIXLD: return &opInfo_PIXLD;
   case OP_POPCNT: return &opInfo_POPC;
   case OP_QUADOP: return &opInfo_FSWZADD;
   case OP_RDSV:
#if 0
      if (targ->isCS2RSV(i->getSrc(0)->reg.data.sv.sv))
         return &opInfo_CS2R;
#endif
      return &opInfo_S2R;
   case OP_SAT: return &opInfo_SAT;
   case OP_SELP: return &opInfo_SEL;
   case OP_SET:
   case OP_SET_AND:
   case OP_SET_OR:
   case OP_SET_XOR:
      if (i->def(0).getFile() != FILE_PREDICATE) {
         if (isFloatType(i->dType)) {
            if (i->dType == TYPE_F32)
               return &opInfo_FSET_BF;
         } else {
            if (isFloatType(i->sType))
                  return &opInfo_FSET;
            return &opInfo_ISET;
         }
      } else {
         if (isFloatType(i->sType))
            if (i->sType == TYPE_F64)
               return &opInfo_DSETP;
            else
               return &opInfo_FSETP;
         else
            return &opInfo_ISETP;
      }
      break;
   case OP_SGXT: return &opInfo_SGXT;
   case OP_SHF: return &opInfo_SHF;
   case OP_SHFL: return &opInfo_SHFL;
   case OP_SHL: return &opInfo_SHL;
   case OP_SHLADD: return &opInfo_LEA;
   case OP_SHR: return &opInfo_SHR;
   case OP_SLCT:
      if (isFloatType(i->sType))
         return &opInfo_FCMP;
      return &opInfo_ICMP;
   case OP_STORE:
      switch (i->src(0).getFile()) {
      case FILE_MEMORY_LOCAL : return &opInfo_STL;
      case FILE_MEMORY_SHARED: return &opInfo_STS;
      case FILE_MEMORY_GLOBAL: return &opInfo_ST;
      default:
         break;
      }
      break;
   case OP_SUB: return &opInfo_SUB;
   case OP_SULDB:
   case OP_SULDP: return &opInfo_SULD;
   case OP_SUREDB:
   case OP_SUREDP: return &opInfo_SUATOM;
   case OP_SUSTB:
   case OP_SUSTP: return &opInfo_SUST;
   case OP_TEX:
   case OP_TXB:
   case OP_TXL: return &opInfo_TEX;
   case OP_TXD: return &opInfo_TXD;
   case OP_TXF: return &opInfo_TLD;
   case OP_TXG: return &opInfo_TLD4;
   case OP_TXLQ: return &opInfo_TMML;
   case OP_TXQ: return &opInfo_TXQ;
   case OP_VFETCH: return &opInfo_ALD;
   case OP_VOTE: return &opInfo_VOTE;
   case OP_WARPSYNC: return &opInfo_WARPSYNC;
   default:
      break;
   }
   return NULL;
}

bool
TargetGV100::isSatSupported(const Instruction *i) const
{
   switch (i->dType) {
   case TYPE_F32:
      switch (i->op) {
      case OP_ADD:
      case OP_FMA:
      case OP_MAD:
      case OP_MUL: return true;
      default:
         break;
      }
      break;
   default:
      break;
   }
   return false;
}

bool
TargetGV100::isModSupported(const Instruction *i, int s, Modifier mod) const
{
   const struct opInfo *info = nv50_ir::getOpInfo(i);
   uint8_t mods = 0;
   if (info && s < (int)ARRAY_SIZE(info->src))
      mods = info->src[s].mods;
   return (mod & Modifier(mods)) == mod;
}

bool
TargetGV100::isOpSupported(operation op, DataType ty) const
{
   if (op == OP_MAD || op == OP_FMA)
      return true;
   if (ty == TYPE_F32) {
      if (op == OP_MAX)
         return true;
   }
   if (op == OP_RSQ)
      return true;
   if (op == OP_SET ||
       op == OP_SET_AND ||
       op == OP_SET_OR ||
       op == OP_SET_XOR)
      return true;
   if (op == OP_SHLADD)
      return true;
   return false;
}

bool
TargetGV100::isUniformSupported(const Instruction *i) const
{
   if (i->asTex())
      return false;

   if (i->op == OP_NOP)
      return true;

   // some SVs are uniform
   if (i->op == OP_RDSV) {
      switch (i->getSrc(0)->reg.data.sv.sv) {
      case SV_CTAID:
         return true;
      default:
         assert(!"SV wrongly marked as uniform");
         break;
      }
   }

   const struct opInfo *info = nv50_ir::getOpInfo(i);
   // if def is uniform, all sources can and must be
   bool uniformWrite = i->defExists(0);
   for (int d = 0; i->defExists(d); ++d) {
      Value *def = i->getDef(d);
      if (!(info->def & 1 << def->reg.file))
         return false;
      uniformWrite &= def->reg.file == FILE_UGPR;
   }

   for (int s = 0; i->srcExists(s); ++s) {
      const ValueRef &src = i->src(s);
      DataFile file = src.getFile();

      // TODO
      if (src.getIndirect(0) || src.getIndirect(1))
         return false;

      // treat all UGPRs as GPRs
      // only allow immediates and UGPRs
      if (uniformWrite) {
         switch (src.getFile()) {
         case FILE_UGPR:
            file = FILE_GPR;
            continue;
         case FILE_UPREDICATE:
            file = FILE_PREDICATE;
            continue;
         case FILE_IMMEDIATE:
            // treat 0 as reg
            if (!src.get()->asImm()->reg.data.u64)
               file = FILE_GPR;
            break;
         case FILE_MEMORY_CONST:
            if (i->op == OP_MOV || i->op == OP_LOAD)
               break;
            return false;
         default:
            return false;
         }
      }

      if (!(info->src[s].files & (1 << file)))
         return false;

      if (uniformWrite)
         continue;

      switch (i->op) {
      case OP_FMA:
      case OP_MAD:
      case OP_PERMT:
      case OP_SHL:
      case OP_SHR:
         if (s == 1 && i->src(1).getFile() == FILE_UGPR && i->src(2).getFile() != FILE_GPR)
            return false;
         else
         if (s == 2 && i->src(2).getFile() == FILE_UGPR && i->src(1).getFile() != FILE_GPR)
            return false;
         break;
      default:
         break;
      }

   }

   return true;
}

bool
TargetGV100::isBarrierRequired(const Instruction *i) const
{
   switch (i->op) {
   case OP_BREV:
      return true;
   default:
      break;
   }

   return TargetGM107::isBarrierRequired(i);
}

bool
TargetGV100::insnCanLoad(const Instruction *i, int s,
                         const Instruction *ld) const
{
   const struct opInfo *info = nv50_ir::getOpInfo(i);
   uint16_t files = 0;

   if (ld->src(0).getFile() == FILE_IMMEDIATE && ld->getSrc(0)->reg.data.u64 == 0)
      return (!i->isPseudo() &&
              !i->asTex() &&
              i->op != OP_EXPORT && i->op != OP_STORE);

   if (ld->src(0).isIndirect(0))
      return false;

   if (info && s < (int)ARRAY_SIZE(info->src)) {
      files = info->src[s].files;
      if ((s == 1 && i->srcExists(2) && !i->src(2).isGPR()) ||
          (s == 2 && i->srcExists(1) && !i->src(1).isGPR())) {
         files &= ~(1 << FILE_MEMORY_CONST);
         files &= ~(1 << FILE_IMMEDIATE);
      } else
      if ((i->op == OP_SHL || i->op == OP_SHR) &&
          ((s == 0 && i->srcExists(1) && !i->src(1).isGPR()) ||
           (s == 1 && i->srcExists(0) && !i->src(0).isGPR()))) {
         files &= ~(1 << FILE_MEMORY_CONST);
         files &= ~(1 << FILE_IMMEDIATE);
      }
   }

   if (ld->src(0).getFile() == FILE_IMMEDIATE) {
      if (i->sType == TYPE_F64) {
         if (ld->getSrc(0)->asImm()->reg.data.u64 & 0x00000000ffffffff)
            return false;
      }
   }

   return (files & (1 << ld->src(0).getFile()));
}

void
TargetGV100::getBuiltinCode(const uint32_t **code, uint32_t *size) const
{
   //XXX: find out why gv100 (tu1xx is fine) hangs without this
   static uint32_t builtin[] = {
      0x0000794d, 0x00000000, 0x03800000, 0x03ffde00,
      0x0000794d, 0x00000000, 0x03800000, 0x03ffde00,
      0x0000794d, 0x00000000, 0x03800000, 0x03ffde00,
      0x0000794d, 0x00000000, 0x03800000, 0x03ffde00,
      0x0000794d, 0x00000000, 0x03800000, 0x03ffde00,
      0x0000794d, 0x00000000, 0x03800000, 0x03ffde00,
      0x0000794d, 0x00000000, 0x03800000, 0x03ffde00,
      0x0000794d, 0x00000000, 0x03800000, 0x03ffde00,
      0x0000794d, 0x00000000, 0x03800000, 0x03ffde00,
      0x0000794d, 0x00000000, 0x03800000, 0x03ffde00,
      0x0000794d, 0x00000000, 0x03800000, 0x03ffde00,
      0x0000794d, 0x00000000, 0x03800000, 0x03ffde00,
      0x0000794d, 0x00000000, 0x03800000, 0x03ffde00,
      0x0000794d, 0x00000000, 0x03800000, 0x03ffde00,
      0x0000794d, 0x00000000, 0x03800000, 0x03ffde00,
      0x0000794d, 0x00000000, 0x03800000, 0x03ffde00,
      0x0000794d, 0x00000000, 0x03800000, 0x03ffde00,
      0x0000794d, 0x00000000, 0x03800000, 0x03ffde00,
      0x0000794d, 0x00000000, 0x03800000, 0x03ffde00,
      0x0000794d, 0x00000000, 0x03800000, 0x03ffde00,
      0x0000794d, 0x00000000, 0x03800000, 0x03ffde00,
      0x0000794d, 0x00000000, 0x03800000, 0x03ffde00,
      0x0000794d, 0x00000000, 0x03800000, 0x03ffde00,
      0x0000794d, 0x00000000, 0x03800000, 0x03ffde00,
      0x0000794d, 0x00000000, 0x03800000, 0x03ffde00,
      0x0000794d, 0x00000000, 0x03800000, 0x03ffde00,
      0x0000794d, 0x00000000, 0x03800000, 0x03ffde00,
   };
   *code = builtin;
   *size = sizeof(builtin);
}

uint32_t
TargetGV100::getBuiltinOffset(int builtin) const
{
   return 0;
}

bool
TargetGV100::runLegalizePass(Program *prog, CGStage stage) const
{
   if (stage == CG_STAGE_PRE_SSA) {
      GM107LoweringPass pass1(prog);
      GV100LoweringPass pass2(prog);
      pass1.run(prog, false, true);
      pass2.run(prog, false, true);
      return true;
   } else
   if (stage == CG_STAGE_SSA) {
      GV100LegalizeSSA pass(prog);
      TU100LegalizeURegs uregs(this);
      bool res = pass.run(prog, false, true);
      uregs.run(prog, true, false);
      return res;
   } else
   if (stage == CG_STAGE_POST_RA) {
      NVC0LegalizePostRA pass(prog);
      return pass.run(prog, false, true);
   }
   return false;
}

CodeEmitter *
TargetGV100::getCodeEmitter(Program::Type type)
{
   return new CodeEmitterGV100(this);
}

TargetGV100::TargetGV100(unsigned int chipset)
   : TargetGM107(chipset)
{
   initOpInfo();
};

Target *getTargetGV100(unsigned int chipset)
{
   return new TargetGV100(chipset);
}

};
