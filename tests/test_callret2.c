#include <assert.h>
#include <stdio.h>
#include <stddef.h>
#include <stdint.h>

#include "../main/main.c"

int main(void)
{
  struct Machine m;
  reset_machine(&m);

  static const uint16_t program[] = {
      0x0C00, 0x1234,
      0x0F80, 0x000A,
      0x0F00, 0x0007,
      0xE780,
      0x0D00, 0x5678,
      0xF800,
      0x0C80, 0xAAAA,
      0xE800,
  };
  memcpy(m.memory, program, sizeof(program));

  m.reg[SP] = 0x8000;
  run_machine(&m);

  assert(m.halted);
  assert(m.fault == FAULT_NONE);
  assert(m.reg[PC] == 10);
  assert(m.reg[R0] == 0x1234);
  assert(m.reg[R1] == 0xAAAA);
  assert(m.reg[R2] == 0x5678);
  assert(m.reg[SP] == 0x8000);
  printf("CALL/RET pseudo-expansion OK\n");
  return 0;
}
