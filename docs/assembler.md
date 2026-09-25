# 汇编器规格

本文档规定宿主工具 `tools/assembler.c` 的命令行接口、汇编语法、编码规则、输出格式和限制。指令执行语义和字节码背景见 [`docs/bytecodes.md`](bytecodes.md)。

## 构建

实现使用 C11，不依赖第三方库。推荐的严格构建命令为：

```sh
cc -std=c11 -Wall -Wextra -Werror -Wpedantic tools/assembler.c -o build/assembler
```

如果 `build/` 尚不存在，先创建它。测试脚本会在临时目录中用同一条严格命令构建工具，不会把宿主可执行文件加入固件构建。

`tools/assembler.py` 仅是旧命令行入口的兼容包装：它优先执行同目录下的 `tools/assembler`，没有已构建文件时临时编译 `assembler.c`，然后原样转发参数。默认实现、测试和本文档均指向 C 工具。`tests/gen_e2e_header.py` 接收 C 可执行文件；为兼容旧调用，传入 `.py` 路径时会通过当前 Python 解释器执行。

## 命令行

```text
assembler [-h] -o OUTPUT [--format {bin,text,hex,addrtext,cmd}] [--symbols] INPUT
```

参数如下：

| 参数 | 含义 |
| --- | --- |
| `INPUT` | 汇编源文件，必需 |
| `-o OUTPUT`, `--output OUTPUT` | 输出文件，必需 |
| `--format bin` | 小端 16-bit 二进制，默认值 |
| `--format text` | 每个 word 一行二进制位串 |
| `--format hex` | 每个 word 一行四位大写十六进制 |
| `--format addrtext` | 带四位十六进制 word 地址的位串 |
| `--format cmd` | 串口控制台加载命令 |
| `--symbols` | 在成功消息后打印入口和符号表 |
| `-h`, `--help` | 打印帮助并退出 |

`--format=hex`、`--output=FILE`、`-oFILE` 和无歧义的长选项前缀也可以使用；独立的 `-h`、`--help` 或未知选项不会被当作 `-o`/`--output`/`--format` 的值。这里只保证本文列出的 CLI 行为，不承诺 Python argparse 的其它扩展。参数错误返回 `2`；源文件、汇编或输出错误返回 `1`；成功返回 `0`。错误诊断以 `Error: ` 开头，主要错误文本保持兼容。

## 源文件语法

源文件必须是有效 UTF-8，且不能包含 NUL 字节；换行可以是 LF 或 CRLF。汇编关键字不区分大小写；符号表中的标签和寄存器名也不区分大小写。词法空白和数字按 ASCII 汇编规则处理。

### 注释和空白

`;` 和 `#` 都可以开始注释，注释从该字符一直延伸到行尾。行内指令前的空白、逗号两侧的空白均被忽略。空行没有汇编含义。

### 标签

标签形式为：

```text
name:
```

名称必须以 ASCII 字母或下划线开头，后续可使用 ASCII 字母、数字和下划线。标签在声明处取得当前 word 地址；同一名称重复声明会报错。一个物理行可以有多个标签：

```text
start: body: LDI 0, R0
```

所有标签在第一遍扫描时登记，因此可以使用前向引用。标签按名称大小写不敏感地比较。标签和寄存器同名时，控制流操作数优先按寄存器解释。

### 数字

值可以写成以下形式：

- 十进制：`0`、`10`、`65535`；
- 十六进制前缀：`0x2A`、`0X2a`；
- 十六进制后缀：`2Ah`；
- 二进制前缀：`0b1010`；
- 八进制前缀：`0o17`；
- 上述形式前可加负号，例如 `-1`、`-0x10`；`- 1` 也按 Python 兼容行为接受。

用于立即数、`.word` 值和标签值的最终范围是 `0..0xFFFF`。负数只有 `-0` 合法；其它负数以及大于 `65535` 的数会报错。标签值必须是已定义标签；控制流指令的目标操作数只接受寄存器或标签，不接受裸数字。程序最多可以有 `0x10000` 个 word；在地址 `0x10000` 定义标签或入口会报错。

### 伪指令

```text
.entry NAME
```

定义程序入口。只能出现一次，且 `NAME` 必须存在；没有 `.entry` 时入口为 `0000h`。

```text
.text
.data
```

两个 section 指令都被接受，但当前实现共享同一个线性 word 地址空间，不产生重定位或独立数据段。指令后的空白文本会被忽略。

```text
.word value1, value2, ...
WORD value1, value2, ...
```

`.word` 和 `WORD` 等价，每个值产生一个 16-bit word；值可以是数字或标签。`.word` 至少需要一个值。

## 指令和编码

地址和 word 都是 16 bit；`PC` 以 word 为单位递增。寄存器编码如下：

| 编码 | 寄存器 | 编码 | 寄存器 |
| ---: | --- | ---: | --- |
| `0` | `ORD0` | `8` | `R0` |
| `1` | `ORD1` | `9` | `R1` |
| `2` | `PC` | `A` | `R2` |
| `3` | `IR` | `B` | `R3` |
| `4` | `MAR` | `C` | `R4` |
| `5` | `SP` | `D` | `R5` |
| `6` | `RS` | `E` | `R6` |
| `7` | `DISPLAY` | `F` | `R7` |

指令首 5 bit 是 opcode。R-type 为 `opcode | source | destination | 3 bit RSV`；U-type 和 J-type 为 `opcode | register | 7 bit RSV`；LDI 的首 word 为 `opcode | destination | 7 bit RSV`，第二 word 是立即数。

### Opcode 表

| Opcode | 助记符 | Opcode | 助记符 |
| ---: | --- | ---: | --- |
| `00000` | `MOV` | `10000` | `NOT` |
| `00001` | `LDI` | `10001` | `SHL` |
| `00010` | `RED` | `10010` | `SHR` |
| `00011` | `WRT` | `10011` | `CMP` |
| `00100` | `PUSH` | `10100` | `JMP` |
| `00101` | `POP` | `10101` | `JZ` |
| `00110` | `MNXT` | `10110` | `JNZ` |
| `00111` | `MPRV` | `10111` | `JN` |
| `01000` | `ADD` | `11000` | `JP` |
| `01001` | `SUB` | `11001` | `JC` |
| `01010` | `DIV` | `11010` | `JNC` |
| `01011` | `INC` | `11011` | `JV` |
| `01100` | `DEC` | `11100` | `CALL` |
| `01101` | `AND` | `11101` | `RET` |
| `01110` | `OR` | `11110` | `NOP` |
| `01111` | `XOR` | `11111` | `HLT` |

### 指令表

| 指令 | 操作数 | 长度 | 说明 |
| --- | --- | ---: | --- |
| `MOV` | `source,destination` | 1 | R-type；`IR` 不能作为 destination |
| `LDI` | `immediate,destination` | 2 | destination 仅允许 `R0` 到 `R7` |
| `RED` | `register` | 1 | U-type |
| `WRT` | `register` | 1 | U-type |
| `PUSH` | `register` | 1 | U-type |
| `POP` | `register` | 1 | U-type |
| `MNXT` | 无 | 1 | opcode `00110` |
| `MPRV` | 无 | 1 | opcode `00111` |
| `ADD` | 无 | 1 | opcode `01000` |
| `SUB` | 无 | 1 | opcode `01001` |
| `DIV` | 无 | 1 | opcode `01010` |
| `INC` | 无 | 1 | opcode `01011` |
| `DEC` | 无 | 1 | opcode `01100` |
| `AND` | 无 | 1 | opcode `01101` |
| `OR` | 无 | 1 | opcode `01110` |
| `XOR` | 无 | 1 | opcode `01111` |
| `NOT` | 无 | 1 | opcode `10000` |
| `SHL` | 无 | 1 | opcode `10001` |
| `SHR` | 无 | 1 | opcode `10010` |
| `CMP` | 无 | 1 | opcode `10011` |
| `JMP` | `register` 或 `label` | 1 或 3 | 标签形式为伪指令 |
| `JZ` | `register` 或 `label` | 1 或 3 | 标签形式为伪指令 |
| `JNZ` | `register` 或 `label` | 1 或 3 | 标签形式为伪指令 |
| `JN` | `register` 或 `label` | 1 或 3 | 标签形式为伪指令 |
| `JP` | `register` 或 `label` | 1 或 3 | 标签形式为伪指令 |
| `JC` | `register` 或 `label` | 1 或 3 | 标签形式为伪指令 |
| `JNC` | `register` 或 `label` | 1 或 3 | 标签形式为伪指令 |
| `JV` | `register` 或 `label` | 1 或 3 | 标签形式为伪指令 |
| `CALL` | `register` 或 `label` | 1 或 5 | 标签形式为伪指令 |
| `RET` | 无 | 1 | opcode `11101` |
| `NOP` | 无 | 1 | opcode `11110` |
| `HLT` | 无 | 1 | opcode `11111` |

`MOV` 的 source 可以是包括 `IR` 在内的所有已定义寄存器，destination 除 `IR` 外均可。`LDI` 的 destination 必须是 `R0`–`R7`。单寄存器指令的操作数也必须是寄存器。

### 标签跳转和 CALL 展开

当 `Jxx` 的目标是标签时，汇编器使用 `R7` 暂存地址：

```text
Jxx LABEL
  -> LDI LABEL, R7
     LDI-value
     Jxx R7
```

其中 `Jxx` 是 `JMP`、`JZ`、`JNZ`、`JN`、`JP`、`JC`、`JNC` 或 `JV`，总长度为 3 words。直接写 `Jxx REGISTER` 时只编码一条 1-word 指令。

`CALL LABEL` 展开为 5 words：

```text
CALL LABEL
  -> LDI LABEL, R7
     target
     LDI return_address, R6
     pc + 5
     CALL R7
```

这里的 `pc` 是整个 `CALL` 伪指令起始 word 地址；`return_address` 指向 5-word 展开后的第一条指令。展开会改写 `R7` 和 `R6`，这是伪指令的明确副作用。`CALL REGISTER` 保持寄存器形式并只生成 1 word。

## 输出格式

无论格式为何，程序最多包含 `0x10000` 个 word；地址空间为 `0x0000..0xFFFF`。恰好 `0x10000` 个普通 `.word` 可以成功生成，但地址 `0x10000` 不能再定义标签或入口，因此不会生成五位 `pc` 地址。成功时首先在标准输出打印：

```text
Assembled N word(s) -> OUTPUT
```

### `bin`

每个 word 输出两个字节，低地址字节在前：

```text
word 0x1234 -> 34 12
```

### `text`

每个 word 一行，拆成两个 8-bit 二进制组：

```text
00000100 10001000
```

### `hex`

每个 word 一行，四位大写十六进制：

```text
1234
ABCD
```

### `addrtext`

每个 word 一行，前面是四位大写 word 地址：

```text
0000: 00000100 10001000
0001: 00000000 00000000
```

### `cmd`

输出串口加载命令流：首行是 `load 0000`，随后每行最多 8 个四位大写十六进制 word，单独一行 `.` 结束加载，最后是入口地址的 `pc XXXX` 命令：

```text
load 0000
0000 0001 0002 0003 0004 0005 0006 0007
.
pc 0000
```

空程序也会输出 `load 0000`、`.` 和 `pc 0000`。

## 符号表

`--symbols` 在成功消息后打印：

```text
Entry: 003Ah
Symbols:
  START                0000h
  BODY                 003Ah
```

标签统一以大写显示，按 word 地址升序排列；地址相同的标签保持声明顺序。地址使用至少四位十六进制并带 `h` 后缀，名称左对齐到 20 列。

## 错误和限制

常见错误包括：无效 UTF-8 或 NUL 源文件、重复标签、重复 `.entry`、未知 `.entry`、标签地址超出 16-bit、未知指令、未知标签、非法寄存器、操作数数量错误、`LDI` 目的寄存器不是 `R0`–`R7`、`MOV` 写入 `IR`、立即数或 `.word` 值超出 16-bit、以及程序超过 64 Ki words。诊断通常包含源行号；I/O 错误包含文件路径。

当前实现不提供表达式、算术常量、宏、变量存储、重定位、压缩或独立的 `.text`/`.data` 布局。输入和输出路径由调用者提供；工具不会修改输入文件以外的仓库文件。

## 调用例子

```sh
cc -std=c11 -Wall -Wextra -Werror -Wpedantic tools/assembler.c -o build/assembler
build/assembler tests/programs/callret.asm -o build/callret.bin
build/assembler tests/programs/loop.asm -o build/loop.cmd --format cmd --symbols
python3 tests/gen_e2e_header.py build/assembler build
sh tests/run_tests.sh
```

`tests/run_tests.sh` 会严格构建 C 汇编器，运行不依赖 Python 汇编器 oracle 的字节级 fixture、格式/错误/边界测试，再使用同一个 C 可执行文件生成端到端头文件和 `test_cmdload` 的命令文件。
