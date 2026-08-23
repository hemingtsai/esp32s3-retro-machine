#include <assert.h>
#include <stdio.h>
#include <stddef.h>
#include <stdint.h>

#include "../main/machine.c"
#include "../main/ui.c"

static struct Machine m;
static struct ControlPanel p;

static void run_command(const char *argv[], int argc)
{
  char *args[8];
  for (int i = 0; i < argc; i++)
    args[i] = (char *)argv[i];
  ui_execute_command(argc, args, &m, &p);
}

int main(void)
{
  reset_machine(&m);
  reset_control_panel(&p);

  /* pc command accepts hex, 0x prefix and h suffix */
  char *pc_args1[] = {"pc", "0020"};
  ui_execute_command(2, pc_args1, &m, &p);
  assert(m.reg[PC] == 0x20);

  char *pc_args2[] = {"pc", "0x40h"};
  ui_execute_command(2, pc_args2, &m, &p);
  assert(m.reg[PC] == 0x40);

  char *pc_args3[] = {"pc", "10h"};
  ui_execute_command(2, pc_args3, &m, &p);
  assert(m.reg[PC] == 0x10);

  char *pc_bad[] = {"pc", "zzz"};
  ui_execute_command(2, pc_bad, &m, &p);
  assert(m.reg[PC] == 0x10);

  /* wr writes a full word */
  char *wr_args[] = {"wr", "2000h", "1234h"};
  ui_execute_command(3, wr_args, &m, &p);
  assert(m.memory[0x2000] == 0x1234);

  /* rd prints; verify it does not modify state */
  uint16_t pc_before = m.reg[PC];
  char *rd_args[] = {"rd", "2000h", "2"};
  ui_execute_command(3, rd_args, &m, &p);
  assert(m.reg[PC] == pc_before);

  /* step executes one instruction */
  static const uint16_t program[] = {0x0C00, 0x0005, 0xF800};
  memcpy(m.memory + 0x30, program, sizeof(program));
  char *pc_args4[] = {"pc", "0030h"};
  ui_execute_command(2, pc_args4, &m, &p);

  enable_instruction_trace(false);
  char *step_args[] = {"step"};
  ui_execute_command(1, step_args, &m, &p);
  assert(m.reg[R0] == 5);
  assert(!m.halted);

  /* run until HLT */
  char *run_args[] = {"run"};
  ui_execute_command(1, run_args, &m, &p);
  assert(m.halted);
  assert(!m.running);

  /* stop pauses before halt */
  reset_machine(&m);
  memcpy(m.memory + 0x30, (uint16_t[]){0xF800}, sizeof(uint16_t));
  char *pc_args5[] = {"pc", "0030h"};
  ui_execute_command(2, pc_args5, &m, &p);
  enable_instruction_trace(false);
  char *stop_args[] = {"stop"};
  ui_execute_command(1, stop_args, &m, &p);
  assert(!m.running && !m.halted);

  /* reset clears CPU but keeps memory */
  char *reset_args[] = {"reset"};
  ui_execute_command(1, reset_args, &m, &p);
  assert(m.reg[PC] == 0 && !m.halted);
  assert(m.memory[0x30] == 0xF800);

  /* unknown command is rejected safely */
  char *bad[] = {"bogus"};
  ui_execute_command(1, bad, &m, &p);

  printf("ui commands OK\n");
  return 0;
}
