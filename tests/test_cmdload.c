// Verifies the bulk load path: assembler --format cmd emits a `load`
// command followed by raw hex words on stdin; feed it through the
// command interpreter and run the program to completion.
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#include "../main/machine.c"
#include "../main/ui.c"

int main(void)
{
  struct Machine m;
  struct ControlPanel p;
  reset_machine(&m);
  reset_control_panel(&p);
  enable_instruction_trace(false);

  char *load_args[] = {"load", "0000"};
  ui_execute_command(2, load_args, &m, &p);

  /* the trailing pc command points at the entry address */
  assert(m.reg[PC] == 0);
  assert(m.memory[0] != 0);

  run_machine(&m);
  assert(m.halted);
  assert(m.fault == FAULT_NONE);
  assert(m.reg[R0] == 1);
  assert(m.flags == FLAG_Z);

  printf("cmd load OK\n");
  return 0;
}
