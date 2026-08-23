#include <string.h>

#include "esp_console.h"
#include "machine.h"
#include "ui.h"

static struct Machine machine;
static struct ControlPanel panel;

static int command_handler(int argc, char **argv)
{
  ui_execute_command(argc, argv, &machine, &panel);
  return 0;
}

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

  static const esp_console_cmd_t commands[] = {
      {.command = "rd", .help = "Read memory words: rd <addr> [count]", .func = command_handler},
      {.command = "wr", .help = "Write memory words: wr <addr> <value> [value...]", .func = command_handler},
      {.command = "load", .help = "Bulk load hex words from stream: load <addr>, '.' to finish", .func = command_handler},
      {.command = "pc", .help = "Set the program counter: pc <addr>", .func = command_handler},
      {.command = "run", .help = "Run until HLT", .func = command_handler},
      {.command = "step", .help = "Execute one instruction", .func = command_handler},
      {.command = "stop", .help = "Stop execution", .func = command_handler},
      {.command = "reset", .help = "Reset the CPU (memory is kept)", .func = command_handler},
      {.command = "info", .help = "Show CPU state", .func = command_handler},
  };

  esp_console_repl_config_t repl_config = ESP_CONSOLE_REPL_CONFIG_DEFAULT();
  repl_config.prompt = "retro>";
  repl_config.max_cmdline_length = 512;

  esp_console_dev_uart_config_t uart_config = ESP_CONSOLE_DEV_UART_CONFIG_DEFAULT();

  esp_console_repl_t *repl = NULL;
  ESP_ERROR_CHECK(esp_console_new_repl_uart(&uart_config, &repl_config, &repl));

  for (size_t i = 0; i < sizeof(commands) / sizeof(commands[0]); i++)
    ESP_ERROR_CHECK(esp_console_cmd_register(&commands[i]));

  ESP_ERROR_CHECK(esp_console_start_repl(repl));
}
