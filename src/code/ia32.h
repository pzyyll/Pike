/*
 * $Id: ia32.h,v 1.8 2001/07/24 09:22:14 hubbe Exp $
 */

#define PIKE_OPCODE_T	unsigned INT8
/* #define ALIGN_PIKE_JUMPS 8 */

#define ins_pointer(PTR)	ins_int((PTR), (void (*)(char))add_to_program)
#define read_pointer(OFF)	read_int(OFF)
#define upd_pointer(OFF, PTR)	upd_int(OFF, PTR)
#define ins_align(ALIGN)	do {				\
    while(Pike_compiler->new_program->num_program % (ALIGN)) {	\
      add_to_program(0);					\
    }								\
  } while(0)
#define ins_byte(VAL)		add_to_program(VAL)
#define ins_data(VAL)		ins_int((VAL), (void (*)(char))add_to_program)


#define MOV2EAX(ADDR) do {				\
    add_to_program(0xa1 /* mov $xxxxx, %eax */);	\
    ins_pointer( (INT32)&(ADDR));			\
}while(0)


#define SET_MEM_REL_EAX(OFFSET, VALUE) do {		\
  INT32 off_ = (OFFSET);				\
  add_to_program(0xc7); /* movl $xxxxx, yy%(eax) */	\
  if(off_) 						\
  {							\
    add_to_program(0x40);				\
    add_to_program(OFFSET);				\
  }else{						\
    add_to_program(0x0);				\
  }							\
  ins_pointer(VALUE);					\
}while(0)

#define REG_IS_SP 1
#define REG_IS_FP 2
#define REG_IS_UNKNOWN -1

extern int ia32_reg_eax;

#define UPDATE_PC() do {				\
    INT32 tmp=PC;					\
    if(ia32_reg_eax != REG_IS_FP)                           \
      MOV2EAX(Pike_interpreter.frame_pointer);		\
    ia32_reg_eax=REG_IS_FP;                                 \
    SET_MEM_REL_EAX(OFFSETOF(pike_frame, pc), tmp);	\
}while(0)


#define READ_INCR_BYTE(PC)	EXTRACT_UCHAR((PC)++)


/* We know that x86 handles unaligned memory access, we might
 * as well use it.
 */
#define RELOCATE_program(P, NEW)	do {				 \
    PIKE_OPCODE_T *op_ = NEW;						 \
    struct program *p_ = P;						 \
    size_t rel_ = p_->num_relocations;					 \
    INT32 delta_ = p_->program - op_;					 \
    if(delta_) {							 \
      while (rel_--) {							 \
        *((INT32 *)(op_ + p_->relocations[rel_]))+=delta_;		 \
      }									 \
    }									 \
  } while(0)


struct dynamic_buffer_s;
struct program;
void ia32_encode_program(struct program *p, struct dynamic_buffer_s *buf);
void ia32_decode_program(struct program *p);

#define ENCODE_PROGRAM(P, BUF)	ia32_encode_program(P, BUF)
#define DECODE_PROGRAM(P)	ia32_decode_program(p)

INT32 ins_f_jump(unsigned int b);
void update_f_jump(INT32 offset, INT32 to_offset);

#define INS_F_JUMP ins_f_jump
#define UPDATE_F_JUMP update_f_jump


void ia32_flush_code_generator(void);
#define FLUSH_CODE_GENERATOR_STATE ia32_flush_code_generator
