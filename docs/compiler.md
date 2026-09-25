# Retro C 编译器

`tools/retrocc.c` 是 Retro C 的宿主编译器。它使用 C11 实现，不依赖 LLVM、libc 之外的系统库，也不参与 ESP32-S3 固件构建。

Retro C 的语言语法和语义见 [`docs/retro-c.md`](retro-c.md)，Retro 汇编器见 [`docs/assembler.md`](assembler.md)，指令系统见 [`docs/bytecodes.md`](bytecodes.md)。

## 1. 工具职责

`retrocc` 负责：

- 读取单个 Retro C 源文件；
- 词法分析；
- 顶层符号预收集；
- 递归下降解析；
- 作用域、类型和表达式检查；
- 为控制流和表达式生成 Retro 汇编；
- 检查生成程序不会侵入保留栈区；
- 输出确定性的 Retro 汇编文本。

`retrocc` 当前不直接输出二进制。标准流水线是：

```text
program.rc
  -> retrocc
  -> program.asm
  -> assembler
  -> program.bin / program.cmd
  -> Retro Machine
```

这样将语言前端/代码生成与指令编码分开，汇编器仍是唯一的机器 word 编码入口。

## 2. 构建

在仓库根目录执行：

```sh
cc -std=c11 -Wall -Wextra -Werror -Wpedantic tools/retrocc.c -o build/retrocc
```

如果 `build/` 不存在，先创建：

```sh
mkdir -p build
```

编译器只依赖标准 C11 头文件。`tools/retrocc.c` 是单一翻译单元，不应加入 `main/CMakeLists.txt`，也不应使用 Xtensa 交叉编译器构建。

## 3. 命令行

```text
retrocc [-h] -o OUTPUT INPUT
```

参数：

| 参数 | 含义 |
| --- | --- |
| `INPUT` | Retro C 源文件 |
| `-o OUTPUT`, `--output OUTPUT` | 汇编输出文件，必需 |
| `-h`, `--help` | 显示帮助并返回 0 |
| `--` | 后续参数作为普通路径处理 |

退出码：

| 退出码 | 含义 |
| ---: | --- |
| 0 | 编译成功 |
| 1 | 读取、词法、语义、代码生成或输出失败 |
| 2 | 命令行参数错误 |

成功时标准输出格式为：

```text
Compiled INPUT -> OUTPUT
```

源文件诊断写到标准错误，并包含路径、行列：

```text
program.rc:7:20: error: function 'add' expects 2 arguments, got 1
```

词法、语义和布局失败时不会创建输出文件。编译器先在内存中完成全部生成和布局检查，成功后才打开输出路径；如果实际写入或关闭文件失败，仍可能留下已创建或不完整的输出文件。

## 4. 最小流水线

创建输入：

```c
u16 add(u16 left, u16 right)
{
    return left + right;
}

u16 main(void)
{
    return add(20, 22);
}
```

编译为 Retro 汇编：

```sh
cc -std=c11 -Wall -Wextra -Werror -Wpedantic tools/retrocc.c -o build/retrocc
build/retrocc examples/retro-c/add.rc -o build/add.asm
```

再汇编为串口加载命令：

```sh
cc -std=c11 -Wall -Wextra -Werror -Wpedantic tools/assembler.c -o build/assembler
build/assembler build/add.asm -o build/add.cmd --format cmd
```

生成内容可以通过串口 REPL 加载。`add.cmd` 最后的 `pc` 命令指向 `__retro_entry`；启动代码设置栈、调用 `main`，然后执行 `HLT`。

## 5. 目标 ABI

### 5.1 寄存器

| 寄存器 | ABI 用途 |
| --- | --- |
| `R0`..`R5` | 第 1 到第 6 个函数参数 |
| `R6` | 编译器临时寄存器 |
| `R7` | 函数地址和伪跳转地址 |
| `RS` | 表达式结果和 `u16` 返回值 |
| `SP` | 当前下降栈指针 |
| `MAR` | load/store 地址 |
| `ORD0` | ALU 左操作数 |
| `ORD1` | ALU 右操作数 |

所有通用寄存器和 flags 都是 caller-saved。Retro C 第一版没有 callee-saved 寄存器。

### 5.2 调用

调用函数时生成原生两指令序列：

```asm
LDI function_name, R7
CALL R7
```

不使用会额外改写 `R6` 的 `CALL label` 汇编器伪指令。

实参先按源顺序求值并压栈，再逆序弹出到 `R0` 到 `R5`。因此嵌套调用不会覆盖已经求值的实参。

`CALL` 将返回 word 地址压入虚拟栈。`RET` 从当前 `SP` 读取返回地址并增加 `SP`。

### 5.3 返回值

- `u16` 函数返回时将值压栈；
- 尾声弹出到 `R0`，调整栈帧后再写回 `RS`；
- `RET` 前 `RS` 已保存最终返回值；
- `void` 函数不定义 `RS`。

### 5.4 栈帧

函数序言按以下顺序执行：

1. 检查进入函数前的活动栈空间；
2. 从 `SP` 扣除完整局部帧；
3. 将 `R0` 到 `R5` 复制到参数槽；
4. 执行业务代码。

帧布局：

```text
SP + 0                  第一个参数或局部变量
SP + 1                  下一个 word
...
SP + frame_words - 1    帧顶
SP + frame_words        调用者栈帧底
```

参数优先占用帧起始槽，之后是按声明顺序分配的局部变量。

编译器解析完函数体、知道真实帧大小后才发射序言。因此函数体后声明的局部变量也会正确计入序言。

## 6. 内存布局

Retro Machine 有 `65536` 个 16-bit word 地址。Retro C 为代码和软件栈保留固定区域：

| 区域 | 地址 | 用途 |
| --- | --- | --- |
| 程序区 | `0x0000..0x7fff` | 启动代码、函数、全局数据、内部 word |
| 最低栈 word | `0x8000` | 表达式临时空间的下界 |
| 临时保护区 | `0x8000..0x83ff` | `PUSH/POP`、实参和 `CALL` 所需空间 |
| 函数栈守卫 | `0x8400` | 新函数和递归调用的安全下界 |
| 活动栈 | `0x8401..0xfffe` | 返回地址、参数帧和局部变量 |
| 初始 `SP` | `0xffff` | 启动栈顶 |

单函数帧最多为 `8192` words。编译器限制生成程序不超过 `0x8000` words，汇编器本身的通用上限仍是 `0x10000` words。

### 6.1 栈守卫

每个函数序言在扣栈前执行等价检查：

```text
SP > 0x8400 + frame_words
```

如果不成立，函数跳到编译器生成的：

```asm
__stack_overflow:
LDI 8400h, R0
MOV R0, RS
HLT
```

这使无界递归和过深调用安全停机，而不是回绕 `SP` 后覆盖程序或调用者帧。该状态不是 `FAULT_DIV_ZERO`；调用方应观察 `HLT` 和 `RS == 0x8400`。

## 7. 代码生成

### 7.1 表达式结果

所有 `u16` 表达式在 `RS` 中留下结果。

标量 load 使用：

```text
address -> MAR
RED RS
```

标量 store 使用：

```text
address -> MAR
WRT source
```

### 7.2 ALU 二元运算

代码生成器将左值和右值放入隐式 ALU 寄存器：

```text
left  -> ORD0
right -> ORD1
ADD / SUB / DIV / AND / OR / XOR / SHL / SHR
result in RS
```

如果右表达式含函数调用，左值会先压栈，避免调用改写 caller-saved 寄存器。

### 7.3 比较

比较先执行 `CMP`，然后使用 `Z` 和无符号减法 `C` flag 规范化结果：

| 表达式 | 条件 |
| --- | --- |
| `left < right` | `C == 1` |
| `left <= right` | `C == 1 || Z == 1` |
| `left > right` | `C == 0 && Z == 0` |
| `left >= right` | `C == 0` |
| `left == right` | `Z == 1` |
| `left != right` | `Z == 0` |

数据指令 `LDI`、`MOV`、`RED` 和 `WRT` 不修改 flags，因此这些分支序列可以安全串联。

### 7.4 短路

`&&` 和 `||` 直接生成条件跳转。右侧表达式仅在需要时生成在控制流中，保证不会提前执行。

### 7.5 控制流

编译器生成 `__cc_N` 形式的内部标签：

- `if/else` 使用条件跳转和 end/else 标签；
- `while` 维护 condition、body 和 end 标签；
- `for` 在源代码中先解析 step 表达式，但代码在 body 后生成；
- `break` 和 `continue` 通过当前最内层循环标签栈解析；
- 嵌套循环恢复外层 loop target。

以 `__` 开头的用户名称被禁止，因此用户符号不会与内部标签冲突。

### 7.6 全局数据

函数和 `__stack_overflow` 标签之后，编译器按源顺序输出：

```asm
global_name:
.word 0000h
```

未初始化全局量输出 0。全局初始化只接受整型常量。

最后输出内部栈守卫 word：

```asm
__stack_limit:
.word 8400h
```

## 8. 编译器结构

`tools/retrocc.c` 包含以下阶段：

1. 动态读取源文件并拒绝 NUL；
2. 在 arena 中分配 token、表达式和实体；
3. 词法分析关键字、标识符、数值、运算符和注释；
4. 预扫描顶层函数/变量并检查重复符号；
5. 递归下降构建表达式 AST；
6. 解析语句并直接生成函数体到临时输出缓冲区；
7. 根据最终帧大小生成函数序言；
8. 拼接前缀、函数体、全局数据和栈守卫；
9. 估算最终 word 数并检查栈区边界；
10. 成功后一次性写出完整汇编文件。

表达式树和解析深度上限均为 256。该限制覆盖括号、一元表达式、平坦二元表达式、右结合赋值和嵌套函数调用，防止深输入导致宿主编译器栈溢出。

内存 arena 按 `max_align_t` 对齐，并在翻译单元结束后统一释放。

## 9. 语义限制

编译器会明确拒绝：

- 缺少 `u16 main(void)`；
- 大小写碰撞的顶层符号；
- 重复活动局部变量；
- 超过 6 个函数参数或实参；
- 超出 `0..65535` 的常量；
- `void` 变量、返回值或非语句表达式；
- 对非左值赋值；
- 循环外 `break`/`continue`；
- `*`、`%` 和其它未实现语法；
- 超过 8192 words 的单函数帧；
- 超过 256 层的表达式；
- 超过保留程序区的生成代码。

完整语言支持列表和未实现功能见 [`docs/retro-c.md`](retro-c.md)。

## 10. 测试

运行全部宿主测试：

```sh
sh tests/run_tests.sh
```

该命令依次执行：

1. 严格构建 C 汇编器；
2. 汇编器格式、错误和边界测试；
3. 严格构建 Retro C 编译器；
4. Retro C 正向端到端测试；
5. Retro C 负向诊断测试；
6. 现有模拟器、UI、控制流和汇编 e2e 测试。

单独运行编译器测试：

```sh
build_dir=$(mktemp -d)
cc -std=c11 -Wall -Wextra -Werror -Wpedantic tools/assembler.c -o "$build_dir/assembler"
sh tests/test_retrocc.sh "$build_dir/assembler" "$build_dir"
rm -rf "$build_dir"
```

正向测试会执行真实的 `retrocc -> assembler -> Machine` 流水线，并检查：

- 算术、位运算、移位和除法；
- `if/else`、`while`、`for`、`break`、`continue`；
- 嵌套调用、六参数传递和递归；
- 全局变量和 void 函数；
- 精确比较和短路；
- 块作用域；
- 栈守卫安全停机。

`tests/compiler_runner.c` 把 little-endian Retro 二进制装入当前 C 模拟器，从 `PC=0` 执行到 `HLT`、fault 或 1,000,000 步上限，并打印 `RS`、通用寄存器和机器状态。

### 10.1 Sanitizer

```sh
build_dir=$(mktemp -d)
cc -std=c11 -Wall -Wextra -Werror -Wpedantic -fsanitize=address,undefined \
  tools/retrocc.c -o "$build_dir/retrocc-sanitize"
cc -std=c11 -Wall -Wextra -Werror -Wpedantic tools/assembler.c -o "$build_dir/assembler"
ASAN_OPTIONS=halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 \
  RETROCC="$build_dir/retrocc-sanitize" \
  sh tests/test_retrocc.sh "$build_dir/assembler" "$build_dir"
rm -rf "$build_dir"
```

## 11. 维护新语法

增加语言功能时建议按以下顺序：

1. 更新 token；
2. 更新递归下降层级；
3. 构建表达式 AST 和语义检查；
4. 在 `RcExpr` 上实现代码生成；
5. 增加正向 Machine e2e；
6. 增加非法输入和行列诊断；
7. 更新语言规范、ABI 文档和限制；
8. 运行严格编译、完整测试和 ASan/UBSan。

如果新表达式需要跨调用保存临时值，应复用 `temporary_depth` 和栈临时区，不要假定 `R0` 到 `R7` 在函数调用后保持不变。
