#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#ifdef ESP_PLATFORM
#include "esp_log.h"
#define MACHINE_LOG(fmt, ...) ESP_LOGI("cpu", fmt, ##__VA_ARGS__)
#else
#define MACHINE_LOG(fmt, ...) printf(fmt "\n", ##__VA_ARGS__)
#endif

#define MEMORY_WORD_COUNT 0x10000

enum Register
{
  ORD0 = 0b0000,
  ORD1 = 0b0001,
  PC = 0b0010,
  IR = 0b0011,
  MAR = 0b0100,
  SP = 0b0101,
  RS = 0b0110,
  DISPLAY = 0b0111,
  R0 = 0b1000,
  R1 = 0b1001,
  R2 = 0b1010,
  R3 = 0b1011,
  R4 = 0b1100,
  R5 = 0b1101,
  R6 = 0b1110,
  R7 = 0b1111
};

enum Flag
{
  FLAG_Z = 1 << 0,
  FLAG_N = 1 << 1,
  FLAG_C = 1 << 2,
  FLAG_V = 1 << 3
};

enum Fault
{
  FAULT_NONE,
  FAULT_DIV_ZERO
};

struct Instruction
{
  enum InstructionType
  {
    R,
    U,
    J,
    I,
    N
  } type;
  enum Opcode
  {
    MOV = 0b00000,
    LDI = 0b00001,
    RED = 0b00010,
    WRT = 0b00011,
    PUSH = 0b00100,
    POP = 0b00101,
    MNXT = 0b00110,
    MPRV = 0b00111,
    ADD = 0b01000,
    SUB = 0b01001,
    DIV = 0b01010,
    INC = 0b01011,
    DEC = 0b01100,
    AND = 0b01101,
    OR = 0b01110,
    XOR = 0b01111,
    NOT = 0b10000,
    SHL = 0b10001,
    SHR = 0b10010,
    CMP = 0b10011,
    JMP = 0b10100,
    JZ = 0b10101,
    JNZ = 0b10110,
    JN = 0b10111,
    JP = 0b11000,
    JC = 0b11001,
    JNC = 0b11010,
    JV = 0b11011,
    CALL = 0b11100,
    RET = 0b11101,
    NOP = 0b11110,
    HLT = 0b11111
  } opcode;
  union
  {
    struct RTypeArguments
    {
      enum Register source;
      enum Register destination;
    } r;
    struct UTypeArguments
    {
      enum Register reg;
    } u;
    struct JTypeArguments
    {
      enum Register target;
    } j;
    struct ITypeArguments
    {
      enum Register destination;
      uint16_t immediate;
    } i;
  };
};

extern const char *const register_names[16];
extern const char *const opcode_names[32];

struct Machine
{
  uint16_t memory[MEMORY_WORD_COUNT];
  uint16_t reg[16];
  enum Flag flags;
  bool halted;
  bool running;
  enum Fault fault;
};

enum PanelDisplay
{
  PANEL_PC,
  PANEL_MAR,
  PANEL_IR,
  PANEL_R0,
  PANEL_R1,
  PANEL_R2,
  PANEL_R3,
  PANEL_RS,
  PANEL_MEMORY
};

enum PanelInputMode
{
  PANEL_LOW_BYTE,
  PANEL_HIGH_BYTE
};

struct ControlPanel
{
  enum PanelDisplay display;
  enum PanelInputMode input_mode;
  uint8_t switches;
  uint16_t input_latch;
};

void reset_cpu(struct Machine *machine);
void reset_machine(struct Machine *machine);
void reset_control_panel(struct ControlPanel *panel);
uint16_t read_register(const struct Machine *machine, enum Register reg);
void write_register(struct Machine *machine, enum Register reg, uint16_t value);
uint16_t panel_read_display(const struct Machine *machine, const struct ControlPanel *panel);
void panel_set_switches(struct ControlPanel *panel, uint8_t switches);
void panel_set_input_mode(struct ControlPanel *panel, enum PanelInputMode mode);
void panel_load(struct ControlPanel *panel);
void panel_examine(struct ControlPanel *panel);
void panel_deposit(struct Machine *machine, const struct ControlPanel *panel);
void panel_mnxt(struct Machine *machine);
void panel_run(struct Machine *machine);
void panel_stop(struct Machine *machine);
bool panel_step(struct Machine *machine);
void panel_reset(struct Machine *machine, struct ControlPanel *panel);
size_t decode_instruction(const uint16_t memory[], uint16_t pc, struct Instruction *instruction);
void format_instruction(const struct Instruction *instruction, char *buffer, size_t size);
void enable_instruction_trace(bool enabled);
bool step_machine(struct Machine *machine);
void run_machine(struct Machine *machine);
