#include <assert.h>
#include <stdio.h>
#include <stddef.h>
#include <stdint.h>

#include "../main/main.c"

int main(void)
{
  struct Machine m;
  struct ControlPanel p;
  panel_reset(&m, &p);

  static const uint16_t program[] = {
      0x0C00, 0x0005,
      0x0C80, 0x0000,
      0x0400,
      0x0488,
      0x5800,
      0xF800,
  };
  memcpy(m.memory, program, sizeof(program));
  m.reg[PC] = 0;

  printf("--- trace on ---\n");
  run_machine(&m);
  assert(m.halted);

  panel_reset(&m, &p);
  memcpy(m.memory, program, sizeof(program));
  enable_instruction_trace(false);
  printf("--- trace off ---\n");
  run_machine(&m);
  assert(m.reg[RS] == 6);

  enable_instruction_trace(true);
  printf("--- fault log ---\n");
  panel_reset(&m, &p);
  uint16_t divprog[] = {0x5000};
  memcpy(m.memory, divprog, sizeof(divprog));
  m.reg[ORD1] = 0;
  m.reg[ORD0] = 9;
  run_machine(&m);
  assert(m.fault == FAULT_DIV_ZERO);

  printf("trace OK\n");
  return 0;
}
