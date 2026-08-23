#include <assert.h>
#include <stdio.h>
#include <stddef.h>
#include <stdint.h>

#include "../main/machine.c"

static struct Machine m;

static void load(const uint16_t program[], size_t size)
{
  reset_machine(&m);
  memcpy(m.memory, program, size);
}

static void set_operands(uint16_t a, uint16_t b)
{
  m.reg[ORD0] = a;
  m.reg[ORD1] = b;
}

static uint16_t prog_add[] = {ADD << 11, HLT << 11};
static uint16_t prog_sub[] = {SUB << 11, HLT << 11};
static uint16_t prog_cmp[] = {CMP << 11, HLT << 11};
static uint16_t prog_div[] = {DIV << 11, HLT << 11};
static uint16_t prog_inc[] = {INC << 11, HLT << 11};
static uint16_t prog_dec[] = {DEC << 11, HLT << 11};
static uint16_t prog_logic[][2] = {{AND << 11, HLT << 11}, {OR << 11, HLT << 11}, {XOR << 11, HLT << 11}, {NOT << 11, HLT << 11}};
static uint16_t prog_shift[][2] = {{SHL << 11, HLT << 11}, {SHR << 11, HLT << 11}};

int main(void)
{
  load(prog_add, sizeof(prog_add));
  set_operands(5, 7);
  run_machine(&m);
  assert(m.reg[RS] == 12);
  assert(m.flags == 0);

  load(prog_add, sizeof(prog_add));
  set_operands(0xFFFF, 1);
  run_machine(&m);
  assert(m.reg[RS] == 0);
  assert(m.flags == (FLAG_Z | FLAG_C));

  load(prog_add, sizeof(prog_add));
  set_operands(0x7FFF, 1);
  run_machine(&m);
  assert(m.reg[RS] == 0x8000);
  assert(m.flags == (FLAG_N | FLAG_V));

  load(prog_sub, sizeof(prog_sub));
  set_operands(7, 10);
  run_machine(&m);
  assert(m.reg[RS] == (uint16_t)-3);
  assert(m.flags == (FLAG_N | FLAG_C));

  load(prog_cmp, sizeof(prog_cmp));
  set_operands(10, 10);
  run_machine(&m);
  assert(m.reg[RS] == 0);
  assert(m.flags == FLAG_Z);

  load(prog_cmp, sizeof(prog_cmp));
  set_operands(0x8000, 1);
  run_machine(&m);
  assert(m.reg[RS] == 0);
  assert(m.flags == FLAG_V);

  load(prog_div, sizeof(prog_div));
  set_operands(9, 4);
  run_machine(&m);
  assert(m.reg[RS] == 2);

  load(prog_div, sizeof(prog_div));
  set_operands(9, 0);
  run_machine(&m);
  assert(m.fault == FAULT_DIV_ZERO);

  load(prog_inc, sizeof(prog_inc));
  set_operands(0xFFFF, 0);
  run_machine(&m);
  assert(m.reg[RS] == 0);
  assert(m.flags == (FLAG_Z | FLAG_C));

  load(prog_dec, sizeof(prog_dec));
  set_operands(0, 0);
  run_machine(&m);
  assert(m.reg[RS] == 0xFFFF);
  assert(m.flags == (FLAG_N | FLAG_C));

  load(prog_logic[0], 4);
  set_operands(0xF0F0, 0x0FF0);
  run_machine(&m);
  assert(m.reg[RS] == 0x00F0);

  load(prog_logic[3], 4);
  set_operands(0xFFFF, 0);
  run_machine(&m);
  assert(m.reg[RS] == 0);
  assert(m.flags == FLAG_Z);

  load(prog_shift[0], 4);
  set_operands(1, 15);
  run_machine(&m);
  assert(m.reg[RS] == 0x8000);
  assert(m.flags == FLAG_N);

  load(prog_shift[0], 4);
  set_operands(1, 16);
  run_machine(&m);
  assert(m.reg[RS] == 0);
  assert(m.flags == FLAG_Z);

  load(prog_shift[1], 4);
  set_operands(0x8000, 4);
  run_machine(&m);
  assert(m.reg[RS] == 0x0800);

  printf("alu OK\n");
  return 0;
}
