#include <ir/ir.h>
#include <target/util.h>

static void sh_runtime() {
  // The POSIX standard doesn't support read -n1, so we read a full line and buffer it.
  // The GETC state machines can be in 3 states:
  //   Non-empty GETC_BUF buffer: take first char from buffer
  //   Empty GETC_BUF buffer AND GETC_BUF_ENDING = -1: need to read a new line
  //   Empty GETC_BUF buffer AND GETC_BUF_ENDING != 0: Need to output the end of line char
  emit_line("GETC_BUF=\"\"");
  emit_line("GETC_BUF_ENDING=-1 # -1: uninit, 0: EOF, 10: newline");
  emit_line("get_char() {");
  emit_line(" if [ -z \"$GETC_BUF\" ]; then");
  emit_line("  if [ $GETC_BUF_ENDING != -1 ]; then");
  emit_line("   : $(( GETC_RES = GETC_BUF_ENDING ))");
  emit_line("   : $(( GETC_BUF_ENDING = -1 ))");
  emit_line("   return");
  emit_line("  fi");
  emit_line("  GETC_BUF_ENDING=10");
  emit_line("  IFS= read -r GETC_BUF || GETC_BUF_ENDING=0");
  emit_line("  if [ -z \"$GETC_BUF\" ]; then");
  emit_line("   : $(( GETC_RES = GETC_BUF_ENDING ))");
  emit_line("   : $(( GETC_BUF_ENDING = -1 ))");
  emit_line("   return");
  emit_line("  fi");
  emit_line(" fi");
  emit_line(" GETC_BUF2=\"${GETC_BUF#?}\"");
  emit_line(" t=\"${GETC_BUF%%\"$GETC_BUF2\"}\"");
  emit_line(" GETC_BUF=\"$GETC_BUF2\"");
  emit_line(" GETC_RES=$(printf '%%d' \"'$t'\")");
  emit_line("}");
}

static void sh_init_state(Data* data) {
  emit_line("#! /bin/sh");
  sh_runtime();
  for (int i = 0; i < 7; i++) {
    emit_line("%s=0", reg_names[i]);
  }
  for (int mp = 0; data; data = data->next, mp++) {
    if (data->v) {
      emit_line("m%d=%d", mp, data->v);
    }
  }
}

const char* sh_value_str(Value* v) {
  if (v->type == REG) {
    return format("$%s", reg_names[v->reg]);
  } else if (v->type == IMM) {
    return format("%d", v->imm);
  } else {
    error("invalid value");
  }
}

static const char* sh_src_str(Inst* inst) {
  return sh_value_str(&inst->src);
}

static const char* sh_cmp_str(Inst* inst, const char * dest) {
  int op = normalize_cond(inst->op, 0);
  const char* op_str;
  switch (op) {
    case JEQ:
      op_str = "=="; break;
    case JNE:
      op_str = "!="; break;
    case JLT:
      op_str = "<"; break;
    case JGT:
      op_str = ">"; break;
    case JLE:
      op_str = "<="; break;
    case JGE:
      op_str = ">="; break;
    case JMP:
      return "1";
    default:
      error("oops");
  }
  if (dest) {
    return format(": $(( %s = $%s %s %s ))",
                  dest, reg_names[inst->dst.reg], op_str, sh_src_str(inst));
  } else {
    return format("$(( $%s %s %s ))",
                  reg_names[inst->dst.reg], op_str, sh_src_str(inst));
  }
}

static void sh_emit_inst(Inst* inst) {
  switch (inst->op) {
  case MOV:
    emit_line("%s=%s", reg_names[inst->dst.reg], sh_src_str(inst));
    break;

  case ADD:
    emit_line(": $(( %s = (($%s + %s) & " UINT_MAX_STR ") ))",
              reg_names[inst->dst.reg],
              reg_names[inst->dst.reg], sh_src_str(inst));
    break;

  case SUB:
    emit_line(": $(( %s = (($%s - %s) & " UINT_MAX_STR ") ))",
              reg_names[inst->dst.reg],
              reg_names[inst->dst.reg], sh_src_str(inst));
    break;

  case LOAD:
    emit_line(": $(( %s = m%s + 0 ))", reg_names[inst->dst.reg], sh_src_str(inst));
    break;

  case STORE:
    emit_line(": $(( m%s = $%s ))", sh_src_str(inst), reg_names[inst->dst.reg]);
    break;

  case PUTC:
    emit_line(": $(( t = (%s & 255) ))", sh_src_str(inst));
    emit_line("printf \\\\$((t/64))$((t/8%%8))$((t%%8))");
    break;

  case GETC:
    emit_line("get_char");
    emit_line(": $(( %s = GETC_RES ))", reg_names[inst->dst.reg]);
    break;

  case EXIT:
    emit_line("exit");
    break;

  case DUMP:
    break;

  case EQ:
  case NE:
  case LT:
  case GT:
  case LE:
  case GE:
    emit_line("%s",
              sh_cmp_str(inst, reg_names[inst->dst.reg]));
    break;

  case JEQ:
  case JNE:
  case JLT:
  case JGT:
  case JLE:
  case JGE:
    emit_line("if [ %s = 1 ]; then", sh_cmp_str(inst, NULL));
    emit_line("  : $(( pc = %s - 1 ))", sh_value_str(&inst->jmp));
    emit_line("fi");
    break;

  case JMP:
    emit_line(": $(( pc = %s - 1 ))", sh_value_str(&inst->jmp));
    break;

  default:
    error("oops");
  }
}

void target_sh(Module* module) {
  sh_init_state(module->data);
  emit_line("");

  emit_line("while :; do");
  emit_line("case $pc in");
  int prev_pc = -1;
  for (Inst* inst = module->text; inst; inst = inst->next) {
    if (prev_pc != inst->pc) {
      if (prev_pc != -1) {
        emit_line(";;");
        dec_indent();
        emit_line("");
      }
      emit_line("%d)", inst->pc);
      inc_indent();
    }
    prev_pc = inst->pc;
    sh_emit_inst(inst);
  }

  emit_line(";;");
  dec_indent();
  emit_line("esac");
  emit_line("");
  emit_line(": $(( pc = pc + 1 ))");
  emit_line("done");
}
