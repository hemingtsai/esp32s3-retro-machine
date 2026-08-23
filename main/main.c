#include <string.h>

#include "machine.h"

static struct Machine machine;
static struct ControlPanel panel;

void app_main(void)
{
  reset_machine(&machine);
  reset_control_panel(&panel);

  static const uint16_t program[] = {
      0x0C00, 0x0005,
      0x0C80, 0x0007,
      0x0400,
      0x0488,
      0x4000,
      0x0350,
      0xF800,
  };
  memcpy(machine.memory + 0x0020, program, sizeof(program));

  machine.reg[PC] = 0x0020;
  panel_run(&machine);
}
