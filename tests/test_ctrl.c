#include <assert.h>
#include <stdio.h>
#include <stddef.h>
#include <stdint.h>

#include "../main/machine.c"

static const uint16_t prog_loop[] = {
  0x0C00,
  0x0003,
  0x0400,
  0x0C80,
  0x0000,
  0x0488,
  0x6000,
  0x0F80,
  0x000E,
  0xAF80,
  0x0340,
  0x0F80,
  0x0002,
  0xA780,
  0xF800,
};
static const uint16_t prog_callret[] = {
  0x0C00,
  0x1234,
  0x0F80,
  0x0008,
  0xE780,
  0x0D00,
  0x5678,
  0xF800,
  0x0C80,
  0xAAAA,
  0xE800,
};
static const uint16_t prog_jztaken[] = {
  0x0C00,
  0x0005,
  0x0400,
  0x0C80,
  0x0005,
  0x0488,
  0x9800,
  0x0F80,
  0x000C,
  0xAF80,
  0x0F00,
  0xBAD0,
  0xF800,
};
static const uint16_t prog_jcnotaken[] = {
  0x0C00,
  0x0005,
  0x0400,
  0x0C80,
  0x0005,
  0x0488,
  0x9800,
  0x0F80,
  0x000C,
  0xCF80,
  0x0E80,
  0x600D,
  0xF800,
};



static struct Machine m;

static void load(const uint16_t program[], size_t size)
{
  reset_machine(&m);
  memcpy(m.memory, program, size);
}

int main(void)
{
  load(prog_loop, sizeof(prog_loop));
  m.reg[SP] = 0x8000;
  run_machine(&m);
  assert(m.halted);
  assert(m.fault == FAULT_NONE);
  assert(m.reg[R0] == 1);
  assert(m.reg[ORD0] == 1);
  assert(m.flags == FLAG_Z);

  load(prog_callret, sizeof(prog_callret));
  m.reg[SP] = 0x8000;
  run_machine(&m);
  assert(m.halted);
  assert(m.reg[PC] == 8);
  assert(m.reg[R0] == 0x1234);
  assert(m.reg[R1] == 0xAAAA);
  assert(m.reg[R2] == 0x5678);
  assert(m.reg[SP] == 0x8000);

  load(prog_jztaken, sizeof(prog_jztaken));
  run_machine(&m);
  assert(m.halted);
  assert(m.reg[R6] == 0);

  load(prog_jcnotaken, sizeof(prog_jcnotaken));
  run_machine(&m);
  assert(m.halted);
  assert(m.reg[R5] == 0x600D);

  printf("control flow OK\n");
  return 0;
}
