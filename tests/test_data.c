#include <assert.h>
#include <stdio.h>
#include <stddef.h>
#include <stdint.h>

#include "../main/main.c"

int main(void)
{
  struct Machine m;
  reset_machine(&m);
  enable_instruction_trace(false);

  uint16_t prog[] = {
      (LDI << 11) | (R0 << 7), 0x1234,
      (LDI << 11) | (R1 << 7), 0x2000,
      (MOV << 11) | (R1 << 7) | (MAR << 3),
      (WRT << 11) | (R0 << 7),
      (MNXT << 11),
      (LDI << 11) | (R2 << 7), 0xBEEF,
      (WRT << 11) | (R2 << 7),
      (MPRV << 11),
      (RED << 11) | (R3 << 7),
      (PUSH << 11) | (R3 << 7),
      (PUSH << 11) | (R2 << 7),
      (POP << 11) | (R4 << 7),
      (POP << 11) | (R5 << 7),
      HLT << 11};
  memcpy(m.memory, prog, sizeof(prog));
  m.reg[SP] = 0x8000;
  run_machine(&m);
  assert(m.reg[PC] == 17);
  assert(m.memory[0x2000] == 0x1234);
  assert(m.memory[0x2001] == 0xBEEF);
  assert(m.reg[MAR] == 0x2000);
  assert(m.reg[R3] == 0x1234);
  assert(m.reg[R4] == 0xBEEF);
  assert(m.reg[R5] == 0x1234);
  assert(m.reg[SP] == 0x8000);

  printf("data ops OK\n");
  return 0;
}
