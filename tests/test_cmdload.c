// Verifies that assembler --format cmd output can be pasted into the
// serial console to load a program: every generated line is fed through
// the command interpreter, then the machine runs to completion.
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#include "../main/machine.c"
#include "../main/ui.c"

#ifndef CMD_FILE
#define CMD_FILE "loop.cmd"
#endif

int main(void)
{
  struct Machine m;
  struct ControlPanel p;
  reset_machine(&m);
  reset_control_panel(&p);
  enable_instruction_trace(false);

  FILE *file = fopen(CMD_FILE, "r");
  assert(file);

  char line[128];
  while (fgets(line, sizeof(line), file))
  {
    char *argv[4] = {0};
    int argc = 0;
    for (char *token = strtok(line, " \t\n"); token && argc < 4; token = strtok(NULL, " \t\n"))
      argv[argc++] = token;
    ui_execute_command(argc, argv, &m, &p);
  }
  fclose(file);

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
