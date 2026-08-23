// End-to-end tests: programs assembled from tests/programs by
// tools/assembler.py are loaded into the simulator and executed.
// Final machine state is asserted against expected results.
#include <assert.h>
#include <stdio.h>
#include <stddef.h>
#include <stdint.h>

#include "../main/machine.c"
#include "e2e_programs.h"

static struct Machine m;

static void load(const uint16_t program[], size_t size)
{
  reset_machine(&m);
  memcpy(m.memory, program, size);
}

static void run_loop_program(void)
{
  load(e2e_loop, sizeof(e2e_loop));
  m.reg[SP] = 0x8000;
  run_machine(&m);

  assert(m.halted);
  assert(m.fault == FAULT_NONE);
  assert(m.reg[ORD0] == 1);
  assert(m.reg[R0] == 1);
  assert(m.flags == FLAG_Z);
}

static void run_callret_program(void)
{
  /* result label is the last word of the program */
  uint16_t result_addr = E2E_CALLRET_SIZE - 1;

  load(e2e_callret, sizeof(e2e_callret));
  m.reg[SP] = 0x8000;
  run_machine(&m);

  assert(m.halted);
  assert(m.fault == FAULT_NONE);
  assert(m.reg[R2] == 42);
  assert(m.memory[result_addr] == 42);
  assert(m.reg[SP] == 0x8000);
}

static void run_memops_program(void)
{
  load(e2e_memops, sizeof(e2e_memops));
  run_machine(&m);

  assert(m.halted);
  assert(m.fault == FAULT_NONE);
  assert(m.reg[R4] == 0xA5);
  assert(m.reg[R5] == 0x5A);
  assert(m.reg[RS] == 0xFF);
  assert(m.flags == 0);
}

int main(void)
{
  enable_instruction_trace(false);

  run_loop_program();
  printf("e2e loop OK\n");

  run_callret_program();
  printf("e2e callret OK\n");

  run_memops_program();
  printf("e2e memops OK\n");

  return 0;
}
