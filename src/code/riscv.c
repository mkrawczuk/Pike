/*
|| This file is part of Pike. For copyright information see COPYRIGHT.
|| Pike is distributed under GPL, LGPL and MPL. See the file COPYING
|| for more information.
*/

#include "global.h"
#include "svalue.h"
#include "operators.h"
#include "bitvector.h"
#include "object.h"
#include "builtin_functions.h"
#include "bignum.h"
#include "constants.h"

#define MACRO  ATTRIBUTE((unused)) static

enum rv_register {
  RV_REG_ZERO,
  RV_REG_RA,
  RV_REG_SP,
  RV_REG_GP,
  RV_REG_TP,
  RV_REG_T0,
  RV_REG_T1,
  RV_REG_T2,
  RV_REG_S0,
  RV_REG_PIKE_SP = RV_REG_S0,
  RV_REG_S1,
  RV_REG_PIKE_FP = RV_REG_S1,
  RV_REG_A0,
  RV_REG_A1,
  RV_REG_A2,
  RV_REG_A3,  
  RV_REG_A4,
  RV_REG_A5,
  RV_REG_A6,
  RV_REG_A7,
  RV_REG_S2,
  RV_REG_PIKE_IP = RV_REG_S2,
  RV_REG_S3,
  RV_REG_PIKE_LOCALS = RV_REG_S3,
  RV_REG_PIKE_GLOBALS = RV_REG_S3,
  RV_REG_S4,
  RV_REG_S5,
  RV_REG_S6,
  RV_REG_S7,
  RV_REG_S8,
  RV_REG_S9,
  RV_REG_S10,
  RV_REG_S11,
  RV_REG_T3,
  RV_REG_T4,
  RV_REG_T5,
  RV_REG_T6,
};

#define RV_IN_RANGE_IMM12(n) (((INT32)(n))>=-2048 && ((INT32)(n))<=2047)

#define RV_R(opcode, funct3, funct7, rd, rs1, rs2)              \
  ((opcode)|((rd)<<7)|((funct3)<<12)|((rs1)<<15)|((rs2)<<20)|((funct7)<<25))
#define RV_I(opcode, funct3, rd, rs1, imm)                      \
  ((opcode)|((rd)<<7)|((funct3)<<12)|((rs1)<<15)|(((imm)&0xfff)<<20))
#define RV_S(opcode, funct3, rs1, rs2, imm)                     \
  ((opcode)|(((imm)&0x1f)<<7)|((funct3)<<12)|((rs1)<<15)|       \
   ((rs2)<<20)|(((imm)&0xfe0)<<(25-5)))
#define RV_U(opcode, rd, imm)                                   \
  ((opcode)|((rd)<<7)|((imm)&~0xfff))
#define RV_J(opcode, rd, imm)					\
  ((opcode)|((rd)<<7)|((imm)&0xff000)|(((imm)&0x800)<<9)|	\
   (((imm)&0x7fe)<<20)|(((imm)&~0xfffff)<<11))
#define RV_B(opcode, funct3, rs1, rs2, imm)				\
  ((opcode)|(((imm)&0x800)>>4)|(((imm)&0x1e)<<7)|((funct3)<<12)|	\
   ((rs1)<<15)|((rs2)<<20)|(((imm)&0x7e0)<<20)|(((imm)&~0xfff)<<19))	\

#define RV_LW_(rd, imm, base) RV_I(3, 2, rd, base, imm)
#define RV_LD_(rd, imm, base) RV_I(3, 3, rd, base, imm)
#define RV_ADDI_(rd, rs, imm) RV_I(19, 0, rd, rs, imm)
#define RV_SLLI_(rd, rs, imm) RV_I(19, 1, rd, rs, imm)
#define RV_XORI(rd, rs, imm) RV_I(19, 4, rd, rs, imm)
#define RV_AUIPC(rd, imm) RV_U(23, rd, imm)
#define RV_SW_(rs, imm, base) RV_S(35, 2, base, rs, imm)
#define RV_SD_(rs, imm, base) RV_S(35, 3, base, rs, imm)
#define RV_ADD_(rd, rs1, rs2) RV_R(51, 0, 0, rd, rs1, rs2)
#define RV_LUI_(rd, imm) RV_U(55, rd, imm)
#define RV_JALR_(rd, base, imm) RV_I(103, 0, rd, base, imm)
#define RV_JAL_(rd, imm) RV_J(111, rd, imm)
#define RV_BEQ_(rs1, rs2, imm) RV_B(99, 0, rs1, rs2, imm)
#define RV_BNE_(rs1, rs2, imm) RV_B(99, 1, rs1, rs2, imm)
#define RV_BLT(rs1, rs2, imm) RV_B(99, 4, rs1, rs2, imm)
#define RV_BGE(rs1, rs2, imm) RV_B(99, 5, rs1, rs2, imm)
#define RV_BLTU(rs1, rs2, imm) RV_B(99, 6, rs1, rs2, imm)
#define RV_BGEU(rs1, rs2, imm) RV_B(99, 7, rs1, rs2, imm)

#ifdef __riscv_compressed

#define RV_IS_C_REG(reg) ((reg)>=RV_REG_S0 && (reg)<=RV_REG_A5)
#define RV_IN_RANGE_CI(n) (((INT32)(n))>=-32 && ((INT32)(n))<=31)
#define RV_IN_RANGE_CS(sz, n) (((INT32)(n))>=0 && ((INT32)(n))<(1<<(5+(sz))) && !(((INT32)(n))&((1<<(sz))-1)))
#define RV_IN_RANGE_CSS(sz, n) (((INT32)(n))>=0 && ((INT32)(n))<(1<<(6+(sz))) && !(((INT32)(n))&((1<<(sz))-1)))
#define RV_IN_RANGE_CB(n) (((INT32)n)>=-256 && ((INT32)n)<=255)
#define RV_IN_RANGE_CJ(n) (((INT32)n)>=-2048 && ((INT32)n)<=2047)

#define RV_CR(op, funct4, rdrs1, rs2) \
  ((op)|((rs2)<<2)|((rdrs1)<<7)|((funct4)<<12))
#define RV_CI(op, funct3, rdrs1, imm1, imm2) \
  ((op)|(((imm1)&0x1f)<<2)|((rdrs1)<<7)|(((imm2)&1)<<12)|((funct3)<<13))
#define RV_CSS(op, funct3, rs2, imm) \
  ((op)|((rs2)<<2)|(((imm)&0x3f)<<7)|((funct3)<<13))
#define RV_CIW(op, funct3, rd, imm) \
  ((op)|(((rd)-8)<<2)|(((imm)&0xff)<<5)|((funct3)<<13))
#define RV_CS(op, funct3, rs1, rs2, imm1, imm2)       \
  ((op)|(((rs2)-8)<<2)|(((rs1)-8)<<7)|((funct3)<<13)| \
   (((imm1)&3)<<5)|(((imm2)&7)<<10))
#define RV_CB(op, funct3, rs1, imm)					\
  ((op)|(((imm)&0x20)>>3)|(((imm)&0x6)<<2)|(((imm)&0xc0)>>1)|		\
   (((rs1)-8)<<7)|(((imm)&0x18)<<7)|(((imm)&0x100)<<4)|((funct3)<<13))
#define RV_CJ(op, funct3, imm)					\
    ((op)|(((imm)&0x20)>>3)|(((imm)&0xe)<<2)|(((imm)&0x80)>>1)|	\
     (((imm)&0x40)<<1)|(((imm)&0x400)>>2)|(((imm)&0x300)<<1)|	\
     (((imm)&0x10)<<7)|(((imm)&0x800)<<1)|((funct3)<<13))

#define RV_C_ADDI4SPN(rd, imm) RV_CIW(0, 0, rd, (((imm)&8)>>3)|(((imm)&4)>>1)|\
				      (((imm)&0x3c0)>>4)|(((imm)&30)<<2))
#define RV_C_LW(rd, imm, base) RV_CS(0, 2, base, rd, ((imm)>>6)|(((imm)&4)>>1), (imm)>>3)
#define RV_C_LD(rd, imm, base) RV_CS(0, 3, base, rd, (imm)>>6, (imm)>>3)
#define RV_C_SW(rs, imm, base) RV_CS(0, 6, base, rs, ((imm)>>6)|(((imm)&4)>>1), (imm)>>3)
#define RV_C_SD(rs, imm, base) RV_CS(0, 7, base, rs, (imm)>>6, (imm)>>3)
#define RV_C_ADDI(rdrs1, imm) RV_CI(1, 0, rdrs1, imm, (imm)>>5)
#define RV_C_JAL(imm) RV_CJ(1, 1, imm)
#define RV_C_LI(rd, imm) RV_CI(1, 2, rd, imm, (imm)>>5)
#define RV_C_LUI(rd, imm) RV_CI(1, 3, rd, (imm)>>12, (imm)>>17)
#define RV_C_ADDI16SP(imm) RV_CI(1, 3, 2, (((imm)&0x20)>>5)|            \
				 (((imm)&0x180)>>6)|(((imm)&0x40)>>3)|  \
				 ((imm)&0x10), (imm)>>9)
#define RV_C_J(imm) RV_CJ(1, 5, imm)
#define RV_C_BEQZ(rs, imm) RV_CB(1, 6, rs, imm)
#define RV_C_BNEZ(rs, imm) RV_CB(1, 7, rs, imm)
#define RV_C_SLLI(rdrs1, imm) RV_CI(2, 0, rdrs1, imm, (imm)>>5)
#define RV_C_LWSP(rd, imm) RV_CI(2, 2, rd, ((imm)>>6)|((imm)&0x1c), (imm)>>5)
#define RV_C_LDSP(rd, imm) RV_CI(2, 3, rd, ((imm)>>6)|((imm)&0x18), (imm)>>5)
#define RV_C_SWSP(rs, imm) RV_CSS(2, 6, rs, ((imm)>>6)|((imm)&0x3c))
#define RV_C_SDSP(rs, imm) RV_CSS(2, 7, rs, ((imm)>>6)|((imm)&0x38))
#define RV_C_JR(rs) RV_CR(2, 8, rs, 0)
#define RV_C_MV(rd, rs) RV_CR(2, 8, rd, rs)
#define RV_C_JALR(rs) RV_CR(2, 9, rs, 0)
#define RV_C_ADD(rdrs1, rs) RV_CR(2, 9, rdrs1, rs)

#define RV_LW(rd, imm, base)						  \
  ((base)==RV_REG_SP && (rd)!=RV_REG_ZERO && RV_IN_RANGE_CSS(2, (imm))?	  \
   RV_C_LWSP((rd), (imm)) :						  \
   (RV_IS_C_REG((rd)) && RV_IS_C_REG((base)) && RV_IN_RANGE_CS(2, (imm))? \
    RV_C_LW((rd), (imm), (base)) : RV_LW_((rd), (imm), (base))))
#define RV_LD(rd, imm, base)						  \
  ((base)==RV_REG_SP && (rd)!=RV_REG_ZERO && RV_IN_RANGE_CSS(3, (imm))?	  \
   RV_C_LDSP((rd), (imm)) :						  \
   (RV_IS_C_REG((rd)) && RV_IS_C_REG((base)) && RV_IN_RANGE_CS(3, (imm))? \
    RV_C_LD((rd), (imm), (base)) : RV_LD_((rd), (imm), (base))))
#define RV_ADDI(rd, rs, imm) \
  ((rd)!=RV_REG_ZERO && (rs)!=RV_REG_ZERO && (imm)==0? RV_C_MV((rd), (rs)) : \
   ((rd)==RV_REG_SP && (rs)==RV_REG_SP &&                                    \
    ((imm)&0xf)==0 && RV_IN_RANGE_CI((imm)>>4)?	RV_C_ADDI16SP((imm)) :       \
    ((rd)==(rs) && RV_IN_RANGE_CI((imm)) && ((rd)!=RV_REG_ZERO || (imm)==0)? \
     RV_C_ADDI((rd), (imm)) :                                                \
     ((rd)!=RV_REG_ZERO && (rs)==RV_REG_ZERO && RV_IN_RANGE_CI((imm))?       \
      RV_C_LI((rd), (imm)) :                                                 \
      (RV_IS_C_REG((rd)) && (rs)==RV_REG_SP && (imm)>0 && (imm)<1024 &&      \
       ((imm)&3)==0? RV_C_ADDI4SPN((rd), (imm)) :                            \
       RV_ADDI_((rd), (rs), (imm)))))))
#define RV_SLLI(rd, rs, imm)						  \
  ((rd) == (rs) && (rd) != RV_REG_ZERO && (imm)>0 && (imm)<__riscv_xlen?  \
   RV_C_SLLI((rd), (imm)) : RV_SLLI_((rd), (rs), (imm)))
#define RV_SW(rs, imm, base)						  \
  ((base)==RV_REG_SP && RV_IN_RANGE_CSS(2, (imm))?			  \
   RV_C_SWSP((rs), (imm)) :						  \
   (RV_IS_C_REG((rs)) && RV_IS_C_REG((base)) && RV_IN_RANGE_CS(2, (imm))? \
    RV_C_SW((rs), (imm), (base)) : RV_SW_((rs), (imm), (base))))
#define RV_SD(rs, imm, base)						  \
  ((base)==RV_REG_SP && RV_IN_RANGE_CSS(3, (imm))?			  \
   RV_C_SDSP((rs), (imm)) :						  \
   (RV_IS_C_REG((rs)) && RV_IS_C_REG((base)) && RV_IN_RANGE_CS(3, (imm))? \
    RV_C_SD((rs), (imm), (base)) : RV_SD_((rs), (imm), (base))))
#define RV_ADD(rd, rs1, rs2)						\
  ((rd)!=RV_REG_ZERO && (rs1)==RV_REG_ZERO && (rs2)==RV_REG_ZERO?	\
   RV_C_LI((rd), 0) :							\
   ((rd)!=RV_REG_ZERO && (rs1)!=RV_REG_ZERO && (rs2)==RV_REG_ZERO?	\
    RV_C_MV((rd), (rs1)) :						\
    ((rd)!=RV_REG_ZERO && (rs1)==RV_REG_ZERO && (rs2)!=RV_REG_ZERO?	\
     RV_C_MV((rd), (rs2)) :						\
     ((rd)!=RV_REG_ZERO && (rs1)==(rd) && (rs2)!=RV_REG_ZERO?		\
      RV_C_ADD((rd), (rs2)) :						\
      ((rd)!=RV_REG_ZERO && (rs1)!=RV_REG_ZERO && (rs2)==(rd)?		\
       RV_C_ADD((rd), (rs1)) : RV_ADD_(rd, rs1, rs2))))))
#define RV_LUI(rd, imm)					\
  ((rd) != RV_REG_ZERO && (rd) != RV_REG_SP &&		\
   ((INT32)(imm))>=-131072 && ((INT32)(imm))<=131071?	\
   RV_C_LUI((rd), (imm)) : RV_LUI_((rd), (imm)))
#define RV_JALR(rd, base, imm)					\
  ((rd) == RV_REG_ZERO && (base) != RV_REG_ZERO && (imm) == 0?	\
   RV_C_JR((base)) :						\
   ((rd) == RV_REG_RA && (base) != RV_REG_ZERO && (imm) == 0?	\
    RV_C_JALR((base)) : RV_JALR_((rd), (base), (imm))))
#define RV_JAL(rd, imm)							\
  ((rd) == RV_REG_ZERO && RV_IN_RANGE_CJ((imm))? RV_C_J((imm)) :	\
   ((rd) == RV_REG_RA && RV_IN_RANGE_CJ((imm))? RV_C_JAL((imm)) :	\
    RV_JAL_((rd), (imm))))
#define RV_BEQ(rs1, rs2, imm)						 \
  ((rs2) == RV_REG_ZERO && RV_IS_C_REG((rs1)) && RV_IN_RANGE_CB((imm)) ? \
   RV_C_BEQZ((rs1), (imm)) : RV_BEQ_((rs1), (rs2), (imm)))
#define RV_BNE(rs1, rs2, imm)						 \
  ((rs2) == RV_REG_ZERO && RV_IS_C_REG((rs1)) && RV_IN_RANGE_CB((imm)) ? \
   RV_C_BNEZ((rs1), (imm)) : RV_BNE_((rs1), (rs2), (imm)))

#else

#define RV_LW RV_LW_
#define RV_LD RV_LD_
#define RV_ADDI RV_ADDI_
#define RV_SLLI RV_SLLI_
#define RV_SW RV_SW_
#define RV_SD RV_SD_
#define RV_ADD RV_ADD_
#define RV_LUI RV_LUI_
#define RV_JALR RV_JALR_
#define RV_JAL RV_JAL_
#define RV_BEQ RV_BEQ_
#define RV_BNE RV_BNE_

#endif


#define RV_MV(rd, rs) RV_ADDI(rd, rs, 0)
#define RV_LI(rd, imm) RV_ADDI(rd, RV_REG_ZERO, imm)
#define RV_NOP() RV_ADDI(RV_REG_ZERO, RV_REG_ZERO, 0)

/* Pointer load/store */
#if __riscv_xlen == 32
#define RV_Lp RV_LW
#define RV_Sp RV_SW
#else
#define RV_Lp RV_LD
#define RV_Sp RV_SD
#endif

#define FLAG_SP_LOADED  1
#define FLAG_FP_LOADED  2
#define FLAG_LOCALS_LOADED 4
#define FLAG_GLOBALS_LOADED 8
#define FLAG_NOT_DESTRUCTED 16

static struct compiler_state {
    unsigned INT32 flags;
} compiler_state;


void riscv_ins_int(INT32 n)
{
#if PIKE_BYTEORDER == 1234
  add_to_program((unsigned INT16)n);
  add_to_program((unsigned INT16)(n >> 16));
#else
  add_to_program((unsigned INT16)(n >> 16));
  add_to_program((unsigned INT16)n);
#endif
}

INT32 riscv_read_int(const PIKE_OPCODE_T *ptr)
{
#ifdef __riscv_compressed
#if PIKE_BYTEORDER == 1234
  return ptr[0] | (ptr[1]<<16);
#else
  return (ptr[0]<<16) | ptr[1];
#endif
#else
  return *(const INT32*)ptr;
#endif
}

void riscv_upd_int(PIKE_OPCODE_T *ptr, INT32 n)
{
#ifdef __riscv_compressed
#if PIKE_BYTEORDER == 1234
  ptr[0] = (unsigned INT16)n;
  ptr[1] = (unsigned INT16)(n >> 16);
#else
  ptr[0] = (unsigned INT16)(n >> 16);
  ptr[1] = (unsigned INT16)n;
#endif
#else
  *(INT32*)ptr = n;
#endif
}

static void rv_emit(unsigned INT32 instr)
{
  add_to_program((unsigned INT16)instr);
  if ((instr & 3) == 3)
    add_to_program((unsigned INT16)(instr >> 16));
}

static void rv_func_epilogue(void)
{
#if __riscv_xlen == 32
  rv_emit(RV_LW(RV_REG_S3, 12, RV_REG_SP));
  rv_emit(RV_LW(RV_REG_S2, 16, RV_REG_SP));
  rv_emit(RV_LW(RV_REG_S1, 20, RV_REG_SP));
  rv_emit(RV_LW(RV_REG_S0, 24, RV_REG_SP));
  rv_emit(RV_LW(RV_REG_RA, 28, RV_REG_SP));
  rv_emit(RV_ADDI(RV_REG_SP, RV_REG_SP, 32));
#else
  rv_emit(RV_LD(RV_REG_S3, 8, RV_REG_SP));
  rv_emit(RV_LD(RV_REG_S2, 16, RV_REG_SP));
  rv_emit(RV_LD(RV_REG_S1, 24, RV_REG_SP));
  rv_emit(RV_LD(RV_REG_S0, 32, RV_REG_SP));
  rv_emit(RV_LD(RV_REG_RA, 40, RV_REG_SP));
  rv_emit(RV_ADDI(RV_REG_SP, RV_REG_SP, 48));
#endif
  rv_emit(RV_JALR(RV_REG_ZERO, RV_REG_RA, 0));
}

void riscv_update_f_jump(INT32 offset, INT32 to_offset)
{
  PIKE_OPCODE_T *op = &Pike_compiler->new_program->program[offset];
  unsigned INT32 instr = op[0];
  to_offset -= offset;
  if ((instr & 3) == 3) {
    instr |= op[1] << 16;
    if ((instr & 0x7f) == 111) {
      /* JAL */
      if (to_offset < -524288 || to_offset > 524287)
	Pike_fatal("riscv_update_f_jump: branch out or range for JAL: %d\n", (int)to_offset);
      instr = RV_J(111, (instr&0xf80)>>7, to_offset*2);
    } else if((instr & 0x7f) == 99) {
      /* BRANCH */
      if (to_offset < -2048 || to_offset > 2047)
	Pike_fatal("riscv_update_f_jump: branch out or range for BRANCH: %d\n", (int)to_offset);
      instr = RV_B(99, (instr & 0x7000)>>12, (instr & 0xf8000)>>15,
		   (instr & 0x1ff00000)>>20, to_offset*2);
    } else {
      Pike_fatal("riscv_update_f_jump on unknown instruction: %x\n", (unsigned)instr);
    }
    op[0] = instr;
    op[1] = instr >> 16;
  } else {
#ifdef __riscv_compressed
    if ((instr & 0x6003) == 0x2001) {
      /* C.JAL / C.J */
      if (to_offset < -1024 || to_offset > 1023)
	Pike_fatal("riscv_update_f_jump: branch out or range for C.JAL/C.J: %d\n", (int)to_offset);
      instr = RV_CJ(1, instr>>13, to_offset*2);
    } else if ((instr & 0xc003) == 0xc001) {
      /* C.BEQZ / C.BNEZ */
      if (to_offset < -128 || to_offset > 127)
	Pike_fatal("riscv_update_f_jump: branch out or range for C.BEQZ/C.BNEZ: %d\n", (int)to_offset);
      instr = RV_CB(1, instr>>13, ((instr & 0x0380)>>7)+8, to_offset*2);
    } else
      Pike_fatal("riscv_update_f_jump on unknown instruction: %x\n", (unsigned)instr);
    op[0] = instr;
#else
    Pike_fatal("riscv_update_f_jump on unknown instruction: %x\n", (unsigned)instr);
#endif
  }
}

int riscv_read_f_jump(INT32 offset)
{
  PIKE_OPCODE_T *op = &Pike_compiler->new_program->program[offset];
  INT32 instr = op[0];
  if ((instr & 3) == 3) {
    instr |= (INT32)(op[1] << 16);
    if ((instr & 0x7f) == 111) {
      /* JAL */
      return offset +
	((((instr>>11)&~0xfffff)|
	  ((instr>>20)&0x7fe)|(((instr)>>9)&0x800)|((instr)&0xff000)) >> 1);
    } else if((instr & 0x7f) == 99) {
      /* BRANCH */
      return offset +
	((((instr>>19)&~0xfff)|((instr>>20)&0x7e0)|
	  ((instr>>7)&0x1e)|((instr<<4)&0x800)) >> 1);
    }
  } else {
#ifdef __riscv_compressed
    if ((instr & 0x6003) == 0x2001) {
      /* C.JAL / C.J */
      instr = (instr << 19)>>19;
      return offset +
	((((instr>>1)&~0x7ff)|((instr>>7)&0x10)|((instr>>1)&0x300)|
	  ((instr<<2)&0x400)|((instr>>1)&0x40)|((instr<<1)&0x80)|
	  ((instr>>2)&0xe)|((instr<<3)&0x20)) >> 1);
    } else if ((instr & 0xc003) == 0xc001) {
      /* C.BEQZ / C.BNEZ */
      instr = (instr << 19)>>19;
      return offset +
	((((instr>>4)&~0xff)|((instr>>7)&0x18)|((instr<<1)&0xc0)|
	  ((instr>>2)&0x6)|((instr<<3)&0x20)) >> 1);
    }
#endif
  }

  Pike_fatal("riscv_read_f_jump on unknown instruction: %x\n", (unsigned)instr);
  return 0;
}

static void rv_update_pcrel(INT32 offset, enum rv_register reg, INT32 delta)
{
  PIKE_OPCODE_T *instr = &Pike_compiler->new_program->program[offset];
  unsigned INT32 first = RV_AUIPC(reg, delta);
  unsigned INT32 second = RV_ADDI_(reg, reg, delta);
  if ((instr[0] & 0xfff) != (first & 0xfff) || instr[2] != (unsigned INT16)second)
    Pike_fatal("rv_update_pcrel on mismatching instruction pair\n");
  instr[0] = first;
  instr[1] = first >> 16;
  instr[3] = second >> 16;
}

static void rv_mov_int32(enum rv_register reg, INT32 val)
{
  if (RV_IN_RANGE_IMM12(val))
    rv_emit(RV_ADDI(reg, RV_REG_ZERO, val));
  else {
    /* Adjust for signedness of lower 12 bits... */
    if (val & 0x800) {
#if __riscv_xlen > 32
      if (val >= 0x7ffff800) {
	/* Adjusting would change the sign bit, need different solution... */
	rv_emit(RV_LUI(reg, ~val));
	rv_emit(RV_XORI(reg, reg, val&0xfff));
	return;
      }
#endif
      val += 0x1000;
    }
    rv_emit(RV_LUI(reg, val));
    if (val & 0xfff)
      rv_emit(RV_ADDI(reg, reg, val&0xfff));
  }
}

MACRO void rv_maybe_update_pc() {
  {
    static int last_prog_id=-1;
    static size_t last_num_linenumbers=(size_t)~0;
    if(last_prog_id != Pike_compiler->new_program->id ||
       last_num_linenumbers != Pike_compiler->new_program->num_linenumbers)
    {
      last_prog_id=Pike_compiler->new_program->id;
      last_num_linenumbers = Pike_compiler->new_program->num_linenumbers;

      UPDATE_PC();
    }
  }
}

static void rv_call(void *ptr)
{
  ptrdiff_t addr = (ptrdiff_t)ptr;
  if (RV_IN_RANGE_IMM12(addr)) {
    rv_emit(RV_JALR(RV_REG_RA, RV_REG_ZERO, addr));
  } else {
#if __riscv_xlen > 32
    INT32 low = (INT32)addr;
    addr >>= 32;
    if (RV_IN_RANGE_IMM12(low)) {
      low &= 0xfff;
      /* Adjust for signedness of lower 12 bits... */
      if ((low & 0x800)) {
	addr++;
      }
    } else {
      /* Adjust for signedness of lower 12 bits... */
      if ((low & 0x800)) {
	low += 0x1000;
      }
      /* Adjust for signedness of lower 32 bits... */
      if (low < 0)
	addr++;
    }
    if (addr) {
      rv_mov_int32(RV_REG_T6, addr);
      rv_emit(RV_SLLI(RV_REG_T6, RV_REG_T6, 32));
      if (low & ~0xfff) {
	rv_emit(RV_LUI(RV_REG_T5, low));
	rv_emit(RV_ADD(RV_REG_T6, RV_REG_T6, RV_REG_T5));
      }
    } else {
      rv_emit(RV_LUI(RV_REG_T6, low));
    }
    rv_emit(RV_JALR(RV_REG_RA, RV_REG_T6, low));
#else
    /* Adjust for signedness of lower 12 bits... */
    if (addr & 0x800)
      addr += 0x1000;
    rv_emit(RV_LUI(RV_REG_T6, addr));
    rv_emit(RV_JALR(RV_REG_RA, RV_REG_T6, addr));
#endif
  }
}

static void rv_call_c_opcode(unsigned int opcode)
{
  void *addr = instrs[opcode-F_OFFSET].address;
  int flags = instrs[opcode-F_OFFSET].flags;

  rv_maybe_update_pc();

  if (opcode == F_CATCH)
    addr = inter_return_opcode_F_CATCH;

  rv_call(addr);

  if (flags & I_UPDATE_SP) {
    compiler_state.flags &= ~FLAG_SP_LOADED;
  }
  if (flags & I_UPDATE_M_SP) {}
  if (flags & I_UPDATE_FP) {
    compiler_state.flags &= ~FLAG_FP_LOADED;
  }
}

void riscv_ins_f_byte(unsigned int opcode)
{
  int flags = instrs[opcode-F_OFFSET].flags;
  INT32 rel_addr = rel_addr;

  switch (opcode) {

  case F_CATCH:
    {
      rel_addr = PIKE_PC;
      rv_emit(RV_AUIPC(RV_REG_A0, 0));
      rv_emit(RV_ADDI_(RV_REG_A0, RV_REG_A0, 0));
    }
    break;

  }

  rv_call_c_opcode(opcode);

  if (flags & I_RETURN) {
    /* Test for -1 */
    rv_emit(RV_ADDI(RV_REG_A5, RV_REG_A0, 1));
    INT32 branch_op = PIKE_PC;
    rv_emit(RV_BNE(RV_REG_A5, RV_REG_ZERO, 0));
    rv_func_epilogue();
    UPDATE_F_JUMP(branch_op, PIKE_PC);

    if (opcode == F_RETURN_IF_TRUE) {
      /* Kludge. We must check if the ret addr is
       * orig_addr + JUMP_EPILOGUE_SIZE. */

      rv_emit(RV_ADDI(RV_REG_T6, RV_REG_RA, 2*JUMP_EPILOGUE_SIZE));
      rel_addr = PIKE_PC;
      rv_emit(RV_BEQ(RV_REG_T6, RV_REG_A0, 0));
    }
  }

  if (flags & I_JUMP) {
    /* This is the code that JUMP_EPILOGUE_SIZE compensates for. */
    rv_emit(RV_JALR(RV_REG_ZERO, RV_REG_A0, 0));

    if (opcode == F_CATCH) {
      rv_update_pcrel(rel_addr, RV_REG_A0, 2*(PIKE_PC - rel_addr));
    } else if (opcode == F_RETURN_IF_TRUE) {
      UPDATE_F_JUMP(rel_addr, PIKE_PC);
    }
  }
}

void riscv_ins_f_byte_with_arg(unsigned int a, INT32 b)
{
  rv_mov_int32(RV_REG_A0, b);
  riscv_ins_f_byte(a);
}

void riscv_ins_f_byte_with_2_args(unsigned int a, INT32 b, INT32 c)
{
  rv_mov_int32(RV_REG_A0, b);
  rv_mov_int32(RV_REG_A1, c);
  riscv_ins_f_byte(a);
}

int riscv_ins_f_jump(unsigned int opcode, int backward_jump)
{
  INT32 ret;

  if (!(instrs[opcode - F_OFFSET].flags & I_BRANCH))
    return -1;

  switch (opcode) {
    case F_BRANCH:
      if (backward_jump) {
	rv_call(branch_check_threads_etc);
      }
      ret = PIKE_PC;
      rv_emit(RV_JAL_(RV_REG_ZERO, 0));
      return ret;
  }

  rv_call_c_opcode(opcode);

  INT32 branch_op = PIKE_PC;
  rv_emit(RV_BEQ(RV_REG_A0, RV_REG_ZERO, 0));

  if (backward_jump) {
    rv_call(branch_check_threads_etc);
  }
  ret = PIKE_PC;
  rv_emit(RV_JAL_(RV_REG_ZERO, 0));

  UPDATE_F_JUMP(branch_op, PIKE_PC);
  
  return ret;
}

int riscv_ins_f_jump_with_arg(unsigned int opcode, INT32 arg1, int backward_jump)
{
  if (!(instrs[opcode - F_OFFSET].flags & I_BRANCH))
    return -1;

  rv_mov_int32(RV_REG_A0, arg1);
  return riscv_ins_f_jump(opcode, backward_jump);
}

int riscv_ins_f_jump_with_2_args(unsigned int opcode, INT32 arg1, INT32 arg2, int backward_jump)
{
  if (!(instrs[opcode - F_OFFSET].flags & I_BRANCH))
    return -1;

  rv_mov_int32(RV_REG_A0, arg1);
  rv_mov_int32(RV_REG_A1, arg2);
  return riscv_ins_f_jump(opcode, backward_jump);
}

void riscv_start_function(int UNUSED(no_pc))
{
}

void riscv_end_function(int UNUSED(no_pc))
{
}

void riscv_ins_entry(void)
{
  /* corresponds to ENTRY_PROLOGUE_SIZE */
#if __riscv_xlen == 32
  rv_emit(RV_ADDI(RV_REG_SP, RV_REG_SP, -32));
  rv_emit(RV_SW(RV_REG_RA, 28, RV_REG_SP));
  rv_emit(RV_SW(RV_REG_S0, 24, RV_REG_SP));
  rv_emit(RV_SW(RV_REG_S1, 20, RV_REG_SP));
  rv_emit(RV_SW(RV_REG_S2, 16, RV_REG_SP));
  rv_emit(RV_SW(RV_REG_S3, 12, RV_REG_SP));
#else
  rv_emit(RV_ADDI(RV_REG_SP, RV_REG_SP, -48));
  rv_emit(RV_SD(RV_REG_RA, 40, RV_REG_SP));
  rv_emit(RV_SD(RV_REG_S0, 32, RV_REG_SP));
  rv_emit(RV_SD(RV_REG_S1, 24, RV_REG_SP));
  rv_emit(RV_SD(RV_REG_S2, 16, RV_REG_SP));
  rv_emit(RV_SD(RV_REG_S3, 8, RV_REG_SP));
#endif
  rv_emit(RV_MV(RV_REG_PIKE_IP, RV_REG_A0));
  riscv_flush_codegen_state();
}

static void riscv_load_fp_reg(void) {
  if (!(compiler_state.flags & FLAG_FP_LOADED)) {
    INT32 offset = OFFSETOF(Pike_interpreter_struct, frame_pointer);
    /* load Pike_interpreter_pointer->frame_pointer into RV_REG_PIKE_FP */
    rv_emit(RV_Lp(RV_REG_PIKE_FP, offset, RV_REG_PIKE_IP));
    compiler_state.flags |= FLAG_FP_LOADED;
    compiler_state.flags &= ~(FLAG_LOCALS_LOADED|FLAG_GLOBALS_LOADED);
  }
}

void riscv_update_pc(void)
{
  rv_emit(RV_AUIPC(RV_REG_A5, 0));
  riscv_load_fp_reg();
  rv_emit(RV_Sp(RV_REG_A5, OFFSETOF(pike_frame, pc), RV_REG_PIKE_FP));
}

void riscv_flush_codegen_state(void)
{
  compiler_state.flags = 0;
}

void riscv_flush_instruction_cache(void *addr, size_t len)
{
  __builtin___clear_cache(addr, (char*)addr+len);
}

void riscv_init_interpreter_state(void)
{
#ifdef PIKE_DEBUG
  /* Check sizes */

  assert(RV_IN_RANGE_IMM12(OFFSETOF(Pike_interpreter_struct, frame_pointer)));
  assert(RV_IN_RANGE_IMM12(OFFSETOF(pike_frame, pc)));
#endif
}


#undef DISASSEMBLE_32BIT
#undef DISASSEMBLE_128BIT
#if __riscv_xlen == 32
#define DISASSEMBLE_32BIT
#elif __riscv_xlen == 128
#define DISASSEMBLE_128BIT
#endif


#define RV_SHIFT(v,n) ((n)<0? ((v)>>-(n)) : ((v)<<(n)))
#define RV_GET_BITS(v,hi,lo,offs) \
  RV_SHIFT((v)&(((1<<((hi)-(lo)+1))-1)<<(offs)),((lo)-(offs)))
#define RV_GET_BIT(v,n,offs) RV_GET_BITS(v,n,n,offs)
#define RV_GET_SIGNBIT(v,n,offs) (((v)&(1<<(offs)))? ~((1<<(n))-1): 0)

#define RV_I_IMM(v) ((INT32)(RV_GET_BITS(v,10,0,20)| \
			     RV_GET_SIGNBIT(v,11,31)))
#define RV_S_IMM(v) ((INT32)(RV_GET_BITS(v,10,5,25)|RV_GET_BITS(v,4,0,7)| \
			     RV_GET_SIGNBIT(v,11,31)))
#define RV_B_IMM(v) ((INT32)(RV_GET_BITS(v,10,5,25)|RV_GET_BITS(v,4,1,8)| \
			     RV_GET_BIT(v,7,11)|RV_GET_SIGNBIT(v,12,31)))
#define RV_U_IMM(v) RV_GET_BITS(v,31-12,12-12,12)
#define RV_J_IMM(v) ((INT32)(RV_GET_BITS(v,10,1,21)|RV_GET_BIT(v,11,20)|  \
			     RV_GET_BITS(v,19,12,12)|RV_GET_SIGNBIT(v,20,31)))
#define RV_CSR_IMM(v) RV_GET_BITS(v,4,0,15)
#define RV_FUNCT3(v) RV_GET_BITS(v,2,0,12)
#define RV_FUNCT7(v) RV_GET_BITS(v,6,0,25)
#define RV_FUNCT12(v) RV_GET_BITS(v,11,0,20)

#define RV_REG(v,offs) RV_GET_BITS(v,4,0,offs)
#define RV_REGD(v) RV_REG(v,7)
#define RV_REGS1(v) RV_REG(v,15)
#define RV_REGS2(v) RV_REG(v,20)
#define RV_CSR(v) RV_GET_BITS(v,11,0,20)

#define RV_CI_IMM(v) ((INT32)(RV_GET_BITS(v,4,0,2)|RV_GET_SIGNBIT(v,5,12)))
#define RV_CIW_IMM(v) (RV_GET_BITS(v,5,4,11)|RV_GET_BITS(v,9,6,7)|          \
		       RV_GET_BIT(v,2,6)|RV_GET_BIT(v,3,5))
#define RV_CB_IMM(v) ((INT32)(RV_GET_BITS(v,4,3,10)|RV_GET_BITS(v,7,6,5)|   \
			      RV_GET_BITS(v,2,1,3)|RV_GET_BIT(v,5,2)|       \
			      RV_GET_SIGNBIT(v,8,12)))
#define RV_CJ_IMM(v) ((INT32)(RV_GET_BIT(v,4,11)|RV_GET_BITS(v,9,8,9)|      \
			      RV_GET_BIT(v,10,8)|RV_GET_BIT(v,6,7)|         \
			      RV_GET_BIT(v,7,6)|RV_GET_BITS(v,3,1,3)|       \
			      RV_GET_BIT(v,5,2)|RV_GET_SIGNBIT(v,11,12)))
#define RV_CFUNCT3(v) RV_GET_BITS(v,2,0,13)

#define RV_REGC(v,offs) (RV_GET_BITS(v,2,0,offs)+8)
#define RV_REGCS2(v) RV_REG(v,2)
#define RV_REGCDS1(v) RV_REG(v,7)
#define RV_REGCD_(v) RV_REGC(v,2)
#define RV_REGCS1_(v) RV_REGC(v,7)
#define RV_REGCS2_(v) RV_REGC(v,2)


static const char *riscv_regname(unsigned n)
{
  static const char * const regnames[] = {
    "zero", "ra", "sp", "gp", "tp", "t0", "t1", "t2",
    "s0", "s1", "a0", "a1", "a2", "a3", "a4", "a5",
    "a6", "a7", "s2", "s3", "s4", "s5", "s6", "s7",
    "s8", "s9", "s10", "s11", "t3", "t4", "t5", "t6",
  };
  return regnames[n&0x3f];
}

static const char *riscv_fregname(unsigned n)
{
  static const char * const regnames[] = {
    "ft0", "ft1", "ft2", "ft3", "ft4", "ft5", "ft6", "ft7",
    "fs0", "fs1", "fa0", "fa1", "fa2", "fa3", "fa4", "fa5",
    "fa6", "fa7", "fs2", "fs3", "fs4", "fs5", "fs6", "fs7",
    "fs8", "fs9", "fs10", "fs11", "ft8", "ft9", "ft10", "ft11",
  };
  return regnames[n&0x3f];
}

void riscv_disassemble_code(PIKE_OPCODE_T *addr, size_t bytes)
{
  static const char * const rvcq1_op[] = { "sub ", "xor ", "or  ", "and ",
					   "subw", "addw" };
  static const char * const load_op[] = {"lb ", "lh ", "lw ", "ld ",
					 "lbu", "lhu", "lwu" };
  static const char * const op_imm_op[] = { "addi", "*sl", "slti", "sltiu",
					    "xori", "*sr", "ori", "andi"};
  static const char * const store_op[] = {"sb", "sh", "sw", "sd"};
  static const char * const op_op[] = {"*add", "sll", "slt", "sltu",
				       "xor", "*srl", "or", "and"};
  static const char * const mul_op[] = {"mul", "mulh", "mulhsu", "mulhu",
					"div", "divu", "rem", "remu"};
  static const char * const branch_op[] = {"beq ", "bne ", NULL, NULL,
					   "blt ", "bge ", "bltu", "bgeu"};
  static const char * const system_op[] = {"rw","rs","rc"};
  const unsigned INT16 *parcel = addr;

  while (bytes >= 2) {
    unsigned INT32 instr = *parcel;
    if ((instr&3) != 3) {
      /* 16-bit format */
      fprintf(stderr, "%p  %04x          ", parcel, instr);
      switch (((instr&3)<<3) | RV_CFUNCT3(instr)) {
      case 0:
	if (!(instr & 0x1f30))
	  fprintf(stderr, "illegal\n");
	else
	  fprintf(stderr, "addi    %s,%s,%d\n",
		  riscv_regname(RV_REGCD_(instr)), riscv_regname(2),
		  (int)RV_CIW_IMM(instr));
	break;
      case 1:
#ifdef DISASSEMBLE_128BIT
	fprintf(stderr, "lq      %s,%d(%s)\n",
		riscv_regname(RV_REGCD_(instr)),
		(int)(RV_GET_BIT(instr,5,12)|RV_GET_BIT(instr,4,11)|
		      RV_GET_BIT(instr,8,10)|RV_GET_BITS(instr,7,6,5)),
		riscv_regname(RV_REGCS1_(instr)));
#else
	fprintf(stderr, "fld     %s,%d(%s)\n",
		riscv_fregname(RV_REGCD_(instr)),
		(int)(RV_GET_BITS(instr,5,3,10)|RV_GET_BITS(instr,7,6,5)),
		riscv_regname(RV_REGCS1_(instr)));
#endif
	break;
      case 2:
	fprintf(stderr, "lw      %s,%d(%s)\n",
		riscv_regname(RV_REGCD_(instr)),
		(int)(RV_GET_BITS(instr,5,3,10)|
		      RV_GET_BIT(instr,2,6)|RV_GET_BIT(instr,6,5)),
		riscv_regname(RV_REGCS1_(instr)));
	break;
      case 3:
#ifdef DISASSEMBLE_32BIT
	fprintf(stderr, "flw     %s,%d(%s)\n",
		riscv_fregname(RV_REGCD_(instr)),
		(int)(RV_GET_BITS(instr,5,3,10)|
		      RV_GET_BIT(instr,2,6)|RV_GET_BIT(instr,6,5)),
		riscv_regname(RV_REGCS1_(instr)));
#else
	fprintf(stderr, "ld      %s,%d(%s)\n",
		riscv_regname(RV_REGCD_(instr)),
		(int)(RV_GET_BITS(instr,5,3,10)|RV_GET_BITS(instr,7,6,5)),
		riscv_regname(RV_REGCS1_(instr)));
#endif
	break;
      case 4:
	  fprintf(stderr, "reserved\n");
	  break;
      case 5:
#ifdef DISASSEMBLE_128BIT
	fprintf(stderr, "sq      %s,%d(%s)\n",
		riscv_regname(RV_REGCS2_(instr)),
		(int)(RV_GET_BIT(instr,5,12)|RV_GET_BIT(instr,4,11)|
		      RV_GET_BIT(instr,8,10)|RV_GET_BITS(instr,7,6,5)),
		riscv_regname(RV_REGCS1_(instr)));
#else
	fprintf(stderr, "fsd     %s,%d(%s)\n",
		riscv_fregname(RV_REGCS2_(instr)),
		(int)(RV_GET_BITS(instr,5,3,10)|RV_GET_BITS(instr,7,6,5)),
		riscv_regname(RV_REGCS1_(instr)));
#endif
	break;
      case 6:
	fprintf(stderr, "sw      %s,%d(%s)\n",
		riscv_regname(RV_REGCS2_(instr)),
		(int)(RV_GET_BITS(instr,5,3,10)|
		      RV_GET_BIT(instr,2,6)|RV_GET_BIT(instr,6,5)),
		riscv_regname(RV_REGCS1_(instr)));
	break;
      case 7:
#ifdef DISASSEMBLE_32BIT
	fprintf(stderr, "fsw     %s,%d(%s)\n",
		riscv_fregname(RV_REGCS2_(instr)),
		(int)(RV_GET_BITS(instr,5,3,10)|
		      RV_GET_BIT(instr,2,6)|RV_GET_BIT(instr,6,5)),
		riscv_regname(RV_REGCS1_(instr)));
#else
	fprintf(stderr, "sd      %s,%d(%s)\n",
		riscv_regname(RV_REGCS2_(instr)),
		(int)(RV_GET_BITS(instr,5,3,10)|RV_GET_BITS(instr,7,6,5)),
		riscv_regname(RV_REGCS1_(instr)));
#endif
	break;
      case 8:
	fprintf(stderr, "addi    %s,%s,%d\n",
		riscv_regname(RV_REGCDS1(instr)),
		riscv_regname(RV_REGCDS1(instr)),
		(int)RV_CI_IMM(instr));
	break;
      case 9:
#ifdef DISASSEMBLE_32BIT
	fprintf(stderr, "jal     %s,%p\n",
		riscv_regname(1),
		((const char *)parcel)+RV_CJ_IMM(instr));
#else
	fprintf(stderr, "addiw   %s,%s,%d\n",
		riscv_regname(RV_REGCDS1(instr)),
		riscv_regname(RV_REGCDS1(instr)),
		(int)RV_CI_IMM(instr));
#endif
	break;
      case 10:
	fprintf(stderr, "addi    %s,%s,%d\n",
		riscv_regname(RV_REGCDS1(instr)),
		riscv_regname(0),
		(int)RV_CI_IMM(instr));
	break;
      case 11:
	if (((instr >> 7) & 31) == 2)
	  fprintf(stderr, "addi    %s,%s,%d\n",
		  riscv_regname(2), riscv_regname(2),
		  (int)(INT32)(RV_GET_BIT(instr,4,6)|RV_GET_BIT(instr,6,5)|
			       RV_GET_BITS(instr,8,7,3)|RV_GET_BIT(instr,5,2)|
			       RV_GET_SIGNBIT(instr,9,12)));
	else
	  fprintf(stderr, "lui     %s,0x%x\n",
		  riscv_regname(RV_REGCDS1(instr)),
		  (unsigned)(RV_CI_IMM(instr)&0xfffff));
	break;
      case 12:
	switch ((instr >> 10) & 3) {
	case 0:
#ifdef DISASSEMBLE_128BIT
	  fprintf(stderr, "srli    %s,%s,%u\n",
		  riscv_regname(RV_REGCDS1(instr)),
		  riscv_regname(RV_REGCDS1(instr)),
		  (unsigned)((instr & 0x107c)?
			     (((instr & 0x1000)? 96 : 0)+
			      RV_GET_BITS(instr,4,0,2)) : 64));
#else
	  fprintf(stderr, "srli    %s,%s,%u\n",
		  riscv_regname(RV_REGCDS1(instr)),
		  riscv_regname(RV_REGCDS1(instr)),
		  (unsigned)((RV_GET_BIT(instr,5,12)|RV_GET_BITS(instr,4,0,2))));
#endif
	  break;
	case 1:
#ifdef DISASSEMBLE_128BIT
	  fprintf(stderr, "srai    %s,%s,%u\n",
		  riscv_regname(RV_REGCDS1(instr)),
		  riscv_regname(RV_REGCDS1(instr)),
		  (unsigned)((instr & 0x107c)?
			     (((instr & 0x1000)? 96 : 0)+
			      RV_GET_BITS(instr,4,0,2)) : 64));
#else
	  fprintf(stderr, "srai    %s,%s,%u\n",
		  riscv_regname(RV_REGCDS1(instr)),
		  riscv_regname(RV_REGCDS1(instr)),
		  (unsigned)((RV_GET_BIT(instr,5,12)|RV_GET_BITS(instr,4,0,2))));
#endif
	  break;
	case 2:
	  fprintf(stderr, "andi    %s,%s,%d\n",
		  riscv_regname(RV_REGCS1_(instr)),
		  riscv_regname(RV_REGCS1_(instr)),
		  (int)RV_CI_IMM(instr));
	  break;
	  break;
	case 3:
	  if ((instr & 0x1000) && ((instr >> 5)&3) >= 2)
	    fprintf(stderr, "???\n");
	  else
	    fprintf(stderr, "%s    %s,%s,%s\n",
		    rvcq1_op[RV_GET_BIT(instr,2,12)|RV_GET_BITS(instr,1,0,5)],
		    riscv_regname(RV_REGCS1_(instr)),
		    riscv_regname(RV_REGCS1_(instr)),
		    riscv_regname(RV_REGCS2_(instr)));
	  break;
	}
	break;
      case 13:
	fprintf(stderr, "j       %s,%p\n",
		riscv_regname(1),
		((const char *)parcel)+RV_CJ_IMM(instr));
	break;
      case 14:
	fprintf(stderr, "beq     %s,%s,%p\n",
		riscv_regname(RV_REGCS1_(instr)),
		riscv_regname(0),
		((const char *)parcel)+RV_CB_IMM(instr));
	break;
      case 15:
	fprintf(stderr, "bne     %s,%s,%p\n",
		riscv_regname(RV_REGCS1_(instr)),
		riscv_regname(0),
		((const char *)parcel)+RV_CB_IMM(instr));
	break;
      case 16:
#ifdef DISASSEMBLE_128BIT
	if (!(instr & 0x107c))
	  fprintf(stderr, "slli    %s,%s,%u\n",
		  riscv_regname(RV_REGCDS1(instr)),
		  riscv_regname(RV_REGCDS1(instr)),
		  64u);
	else
#endif
	  fprintf(stderr, "slli    %s,%s,%u\n",
		  riscv_regname(RV_REGCDS1(instr)),
		  riscv_regname(RV_REGCDS1(instr)),
		  (unsigned)((RV_GET_BIT(instr,5,12)|RV_GET_BITS(instr,4,0,2))));
	break;
      case 17:
#ifdef DISASSEMBLE_128BIT
	fprintf(stderr, "lq      %s,%d(%s)\n",
		riscv_regname(RV_REGCDS1(instr)),
		(int)(RV_GET_BIT(instr,5,12)|
		      RV_GET_BIT(instr,4,6)|RV_GET_BITS(instr,9,6,2)),
		riscv_regname(2));
#else
	fprintf(stderr, "fld     %s,%d(%s)\n",
		riscv_fregname(RV_REGCDS1(instr)),
		(int)(RV_GET_BIT(instr,5,12)|
		      RV_GET_BITS(instr,4,3,5)|RV_GET_BITS(instr,8,6,2)),
		riscv_regname(2));
#endif
	break;
      case 18:
	fprintf(stderr, "lw      %s,%d(%s)\n",
		riscv_regname(RV_REGCDS1(instr)),
		(int)(RV_GET_BIT(instr,5,12)|
		      RV_GET_BITS(instr,4,2,4)|RV_GET_BITS(instr,7,6,2)),
		riscv_regname(2));
	break;
      case 19:
#ifdef DISASSEMBLE_32BIT
	fprintf(stderr, "flw     %s,%d(%s)\n",
		riscv_fregname(RV_REGCDS1(instr)),
		(int)(RV_GET_BIT(instr,5,12)|
		      RV_GET_BITS(instr,4,2,4)|RV_GET_BITS(instr,7,6,2)),
		riscv_regname(2));
#else
	fprintf(stderr, "ld      %s,%d(%s)\n",
		riscv_regname(RV_REGCDS1(instr)),
		(int)(RV_GET_BIT(instr,5,12)|
		      RV_GET_BITS(instr,4,3,5)|RV_GET_BITS(instr,8,6,2)),
		riscv_regname(2));
#endif
	break;
      case 20:
	if (RV_REGCS2(instr)) {
	  fprintf(stderr, "add     %s,%s,%s\n",
		  riscv_regname(RV_REGCDS1(instr)),
		  riscv_regname((instr & 0x1000)? RV_REGCDS1(instr) : 0),
		  riscv_regname(RV_REGCS2(instr)));
	} else if ((instr & 0x1000) && !RV_REGCDS1(instr)) {
	  fprintf(stderr, "ebreak\n");
	} else {
	  fprintf(stderr, "jalr    %s,%s,0\n",
		  riscv_regname((instr & 0x1000)? 1 : 0),
		  riscv_regname(RV_REGCDS1(instr)));
	}
	break;
      case 21:
#ifdef DISASSEMBLE_128BIT
	fprintf(stderr, "sq      %s,%d(%s)\n",
		riscv_regname(RV_REGCS2(instr)),
		(int)(RV_GET_BITS(instr,5,4,11)|RV_GET_BITS(instr,9,6,7)),
		riscv_regname(2));
#else
	fprintf(stderr, "fsd     %s,%d(%s)\n",
		riscv_fregname(RV_REGCS2(instr)),
		(int)(RV_GET_BITS(instr,5,3,10)|RV_GET_BITS(instr,8,6,7)),
		riscv_regname(2));
#endif
	break;
      case 22:
	fprintf(stderr, "sw      %s,%d(%s)\n",
		riscv_regname(RV_REGCS2(instr)),
		(int)(RV_GET_BITS(instr,5,2,9)|RV_GET_BITS(instr,7,6,7)),
		riscv_regname(2));
	break;
      case 23:
#ifdef DISASSEMBLE_32BIT
	fprintf(stderr, "fsw     %s,%d(%s)\n",
		riscv_fregname(RV_REGCS2(instr)),
		(int)(RV_GET_BITS(instr,5,2,9)|RV_GET_BITS(instr,7,6,7)),
		riscv_regname(2));
#else
	fprintf(stderr, "sd      %s,%d(%s)\n",
		riscv_regname(RV_REGCS2(instr)),
		(int)(RV_GET_BITS(instr,5,3,10)|RV_GET_BITS(instr,8,6,7)),
		riscv_regname(2));
#endif
	break;
      }
      bytes -= 2;
      parcel++;
    } else if((instr&0x1f) != 0x1f) {
      /* 32-bit format */
      if (bytes < 4) break;
      instr |= ((unsigned INT32)parcel[1])<<16;
      fprintf(stderr, "%p  %08x      ", parcel, instr);
      switch ((instr >> 2) & 0x1f) {
      case 0:
	if (RV_FUNCT3(instr) <= 6)
	  fprintf(stderr, "%s     %s,%d(%s)\n",
		  load_op[RV_FUNCT3(instr)],
		  riscv_regname(RV_REGD(instr)),
		  (int)RV_I_IMM(instr),
		  riscv_regname(RV_REGS1(instr)));
	else
	  fprintf(stderr, "???\n");
	break;
      case 3:
	switch(RV_FUNCT3(instr)) {
	case 0:
	  fprintf(stderr, "fence   %u,%u\n",
		  (unsigned)RV_GET_BITS(instr,3,0,24),
		  (unsigned)RV_GET_BITS(instr,3,0,20));
	  break;
	case 1:
	  fprintf(stderr, "fence.i\n");
	  break;
	default:
	  fprintf(stderr, "???\n");
	  break;
	}
	break;
      case 4:
      case 6:
	{
	  const char *op = op_imm_op[RV_FUNCT3(instr)];
	  if (*op == '*') {
	    fprintf(stderr, "%s%ci%c   %s,%s,%u\n", op+1,
		    ((instr & 0x40000000)? 'a':'l'),
		    ((instr&4)? 'w':' '),
		    riscv_regname(RV_REGD(instr)),
		    riscv_regname(RV_REGS1(instr)),
		    (unsigned)RV_GET_BITS(instr,5,0,20));
	  } else
	    fprintf(stderr, "%s%-*c%s,%s,%d\n", op,
		    (int)(8-strlen(op)), ((instr&4)? 'w':' '),
		    riscv_regname(RV_REGD(instr)),
		    riscv_regname(RV_REGS1(instr)),
		    (int)RV_I_IMM(instr));
	}
	break;
      case 5:
	fprintf(stderr, "auipc   %s,0x%x\n",
		riscv_regname(RV_REGD(instr)),
		(unsigned)RV_U_IMM(instr));
	break;
      case 8:
	if (RV_FUNCT3(instr) <= 3)
	  fprintf(stderr, "%s      %s,%d(%s)\n",
		  store_op[RV_FUNCT3(instr)],
		  riscv_regname(RV_REGS2(instr)),
		  (int)RV_S_IMM(instr),
		  riscv_regname(RV_REGS1(instr)));
	else
	  fprintf(stderr, "???\n");
	break;
	break;
      case 12:
      case 14:
	{
	  const char *op =
	    ((instr & 0x02000000)? mul_op:op_op)[RV_FUNCT3(instr)];
	  if (*op == '*') {
	    op++;
	    if (instr & 0x40000000)
	      op = (*op == 'a'? "sub":"sra");
	  }
	  fprintf(stderr, "%s%-*c%s,%s,%s\n", op,
		  (int)(8-strlen(op)), ((instr&4)? 'w':' '),
		  riscv_regname(RV_REGD(instr)),
		  riscv_regname(RV_REGS1(instr)),
		  riscv_regname(RV_REGS2(instr)));
	}
	break;
      case 13:
	fprintf(stderr, "lui     %s,0x%x\n",
		riscv_regname(RV_REGD(instr)),
		(unsigned)RV_U_IMM(instr));
	break;
      case 24:
	if (branch_op[RV_FUNCT3(instr)])
	  fprintf(stderr, "%s    %s,%s,%p\n",
		  branch_op[RV_FUNCT3(instr)],
		  riscv_regname(RV_REGS1(instr)),
		  riscv_regname(RV_REGS2(instr)),
		  ((const char *)parcel)+RV_B_IMM(instr));
	else
	  fprintf(stderr, "???\n");
	break;
      case 25:
	fprintf(stderr, "jalr    %s,%s,%d\n",
		riscv_regname(RV_REGD(instr)),
		riscv_regname(RV_REGS1(instr)),
		(int)RV_I_IMM(instr));
	break;
      case 27:
	fprintf(stderr, "jal     %s,%p\n",
		riscv_regname(RV_REGD(instr)),
		((const char *)parcel)+RV_J_IMM(instr));
	break;
      case 28:
	switch(RV_FUNCT3(instr)) {
	case 0:
	  switch(RV_FUNCT12(instr)) {
	  case 0:
	    fprintf(stderr, "ecall\n");
	    break;
	  case 1:
	    fprintf(stderr, "ebreak\n");
	    break;
	  default:
	    fprintf(stderr, "???\n");
	    break;
	  }
	  break;
	case 1:
	case 2:
	case 3:
	  fprintf(stderr, "csr%s   %s,%u,%s\n",
		  system_op[RV_FUNCT3(instr)-1],
		  riscv_regname(RV_REGD(instr)),
		  (unsigned)RV_CSR(instr),
		  riscv_regname(RV_REGS1(instr)));
	  break;
	case 5:
	case 6:
	case 7:
	  fprintf(stderr, "csr%si  %s,%u,%u\n",
		  system_op[RV_FUNCT3(instr)-5],
		  riscv_regname(RV_REGD(instr)),
		  (unsigned)RV_CSR(instr),
		  (unsigned)RV_CSR_IMM(instr));
	  break;
	default:
	  fprintf(stderr, "???\n");
	}
	break;
      case 1:  /* LOAD-FP */
      case 9:  /* STORE-FP */
      case 11: /* AMO */
      case 16: /* MADD */
      case 17: /* MSUB */
      case 18: /* NMSUB */
      case 19: /* NMADD */
      case 20: /* OP-FP */
      default:
	fprintf(stderr, "???\n");
      }
      bytes -= 4;
      parcel+=2;
    } else {
      /* > 32-bit format */
      unsigned parcel_count, i;
      if (!(instr & 0x20))
	parcel_count = 3; /* 48-bit */
      else if(!(instr & 0x40))
	parcel_count = 4; /* 64-bit */
      else if((instr & 0x7000) != 0x7000)
	parcel_count = 5 + ((instr >> 12) & 7); /* 80-176-bit */
      else
	parcel_count = 1; /* Reserved for >=192-bit, not defined... */
      if (bytes < parcel_count * 2)
	break;
      fprintf(stderr, "%p  ", parcel);
      for (i = parcel_count; i > 0; ) {
	fprintf(stderr, "%04x", parcel[--i]);
      }
      fprintf(stderr, "\n");
      bytes -= parcel_count * 2;
      parcel += parcel_count;
    }
  }
  if (bytes > 0) {
    const unsigned char *rest = (const unsigned char *)parcel;
    while(bytes > 0) {
      fprintf(stderr, "%p  %02x\n", rest, *rest);
      --bytes;
      rest++;
    }
  }
}
