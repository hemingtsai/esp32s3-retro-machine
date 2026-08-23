#include <assert.h>
#include <stdio.h>
#include <stddef.h>
#include <stdint.h>

#include "../main/machine.c"

static struct Machine m;
static struct ControlPanel p;

/* 通过面板把一个 16 位字写入 MAR 指向的内存 */
static void deposit_word(uint16_t word)
{
  panel_set_input_mode(&p, PANEL_LOW_BYTE);
  panel_set_switches(&p, (uint8_t)(word & 0xFF));
  panel_load(&p);
  panel_set_input_mode(&p, PANEL_HIGH_BYTE);
  panel_set_switches(&p, (uint8_t)(word >> 8));
  panel_load(&p);
  panel_deposit(&m, &p);
  panel_mnxt(&m);
}

int main(void)
{
  panel_reset(&m, &p);

  /* 程序: LDI 5,R0; LDI 7,R1; MOV R0,ORD0; MOV R1,ORD1; ADD; MOV RS,R2; HLT
     先用拨杆把起始地址 0x0020 装进 MAR */
  panel_set_switches(&p, 0x20);
  panel_load(&p);                       /* low = 0x20 */
  panel_set_input_mode(&p, PANEL_HIGH_BYTE);
  panel_set_switches(&p, 0x00);
  panel_load(&p);                       /* latch = 0x0020 */
  m.reg[MAR] = p.input_latch;

  static const uint16_t program[] = {
      0x0C00, 0x0005,
      0x0C80, 0x0007,
      0x0400,
      0x0488,
      0x4000,
      0x0350,
      0xF800,
  };
  for (size_t i = 0; i < sizeof(program) / sizeof(program[0]); i++)
    deposit_word(program[i]);
  assert(m.reg[MAR] == 0x0029);

  /* RESET 复位 CPU，程序保留，PC 从 0 改为入口 0x20 */
  panel_reset(&m, &p);
  assert(m.memory[0x0020] == 0x0C00);
  m.reg[PC] = 0x0020;

  /* STEP 三次: LDI R0 / LDI R1 / MOV R0->ORD0 */
  assert(panel_step(&m));
  assert(m.reg[R0] == 5);
  assert(panel_step(&m));
  assert(m.reg[R1] == 7);
  assert(panel_step(&m));
  assert(m.reg[ORD0] == 5);

  /* EXAMINE 看 IR: 当前指令是 MOV R1,ORD1 (0x0488) */
  panel_examine(&p);
  assert(panel_read_display(&m, &p) == m.memory[m.reg[MAR]]);

  /* RUN 到停机 */
  p.display = PANEL_PC;
  panel_run(&m);
  assert(m.halted && !m.running);
  assert(m.reg[R2] == 12);

  /* 切到 RS 灯看结果 */
  p.display = PANEL_RS;
  assert(panel_read_display(&m, &p) == 12);

  printf("panel workflow OK\n");
  return 0;
}
