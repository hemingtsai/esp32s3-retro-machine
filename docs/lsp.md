# Retro C 语言服务器

本文说明 Retro C 的语言服务器 [`tools/retrolsp.c`](../tools/retrolsp.c)。它在编辑器与
Retro C 编译器之间提供语法高亮、诊断、大纲、悬浮信息、跳转定义、补全和函数签名帮助。

语言本身见 [`docs/retro-c.md`](retro-c.md)，编译器见 [`docs/compiler.md`](compiler.md)。

## 设计

语言服务器不自己解析 Retro C。每次文档变化时，它调用编译器的
[`--analyze`](compiler.md) 模式，把词法单元、注释区间、符号、引用、作用域和诊断读成
JSON，再映射为 LSP 响应。Retro C 的词法规则、作用域和语义检查因此只有一份实现，
编辑器显示的结果与 `retrocc` 完全一致。

```text
编辑器 <--JSON-RPC over stdio--> retrolsp
                                    |  retrocc --analyze -o -
                                    v
                             分析 JSON（token/comment/symbol/reference/scope/diagnostic）
```

## 构建

```sh
mkdir -p build
cc -std=c11 -Wall -Wextra -Werror -Wpedantic tools/retrolsp.c -o build/retrolsp
cc -std=c11 -Wall -Wextra -Werror -Wpedantic tools/retrocc.c -o build/retrocc
```

两个文件都是单一翻译单元，只依赖标准 C11 头文件和 POSIX 接口，不应加入
`main/CMakeLists.txt`，也不应使用 Xtensa 交叉编译器构建。

## 命令行

```text
usage: retrolsp [-h] [--stdio] [--compiler PATH] [--debounce MS]
```

| 参数 | 说明 |
| --- | --- |
| `--stdio` | 通过标准输入输出通信；这是唯一支持的传输方式，也是默认值 |
| `--compiler PATH` | `retrocc` 可执行文件路径，默认 `retrocc`（按 `PATH` 查找） |
| `--debounce MS` | 文档变化后延迟多少毫秒再分析，默认 `200` |
| `-h`、`--help` | 打印用法并返回 `0` |

退出码：`0` 正常退出或 `--help`，`1` 未收到 `shutdown` 就收到 `exit`，
`2` 命令行参数错误。

编辑器通常以工作区根目录为当前目录启动服务器，因此 `retrocc` 需要在 `PATH` 中，
或者用 `--compiler` 指定绝对路径。

## 客户端配置

通用配置只需要启动命令和文件关联：

```json
{
  "command": "build/retrolsp",
  "args": ["--stdio", "--compiler", "build/retrocc"],
  "filetypes": ["retro-c"],
  "extensions": [".rc"]
}
```

可复制的片段见 [`contrib/retrolsp-settings.json`](../contrib/retrolsp-settings.json)。
仓库的 `.vscode/settings.json` 被忽略，因此客户端配置以 `contrib/` 中的片段为准。

## 支持的能力

| 能力 | 说明 |
| --- | --- |
| `textDocumentSync` | 全文同步（`1`） |
| `semanticTokensProvider` | 语义高亮，图例见下文 |
| `publishDiagnostics` | 编译器诊断，随文档变化推送 |
| `documentSymbolProvider` | 大纲：函数和全局变量，函数下挂参数和局部变量 |
| `hoverProvider` | 悬浮信息，Markdown 代码块显示完整签名 |
| `definitionProvider` | 跳转定义，支持声明和引用 |
| `completionProvider` | 关键字、函数、全局变量和当前位置可见的局部变量 |
| `signatureHelpProvider` | 函数签名与当前参数 |

未实现的方法返回 `-32601`；请求未知文档返回 `-32602`。

### 语义高亮图例

图例只用 LSP 标准类型，索引与 `tokens[].type` 一一对应：

| 索引 | 类型 | 含义 |
| --- | --- | --- |
| 0 | `type` | `u16`、`void` |
| 1 | `variable` | 标量变量和无法识别的标识符 |
| 2 | `parameter` | 函数参数 |
| 3 | `function` | 函数名 |
| 4 | `property` | 数组 |
| 5 | `keyword` | `if`、`for`、`return` 等 |
| 6 | `number` | 整数字面量 |
| 7 | `comment` | 行注释和块注释 |
| 8 | `operator` | 运算符 |
| 9 | `punctuation` | 括号、花括号、分号、逗号 |
| 10 | `macro` | 以 `__` 开头的保留标识符 |
| 11 | `invalid` | 词法无法识别的字符 |

## 位置编码

分析 JSON 中的位置是 1 起始的行列和字节偏移，Retro C 的列按字节计数。
LSP 使用 0 起始、UTF-16 code unit 的位置，服务器按行把字节偏移换算成 UTF-16 单元，
因此中文注释不会让高亮和诊断错位。语义 token 的长度同样是 UTF-16 单元数，
跨行块注释会按行拆成多个 token。

## 已知限制

- 编译器遇到第一个错误就停止，因此每次只推送一条诊断。这是编译器的既有行为，
  语言服务器不做错误恢复。
- 补全按记录的作用域区间判断可见性。函数体、`for` 语句和花括号块各有作用域，
  块外不会提供块内声明的变量。
- 尚未实现 `formatting`、`rename`、`references` 和代码操作。
- 每次分析会启动一次 `retrocc` 进程。Retro C 文件很小，配合 `--debounce` 足够快。
- 传输层只处理 `Content-Length` 头。缺少 `Content-Length` 的头部块会被跳过，
  无法解析为 JSON 的消息体会被忽略；字节层面已经失步的流无法恢复。

## 测试

```sh
sh tests/test_lsp.sh ASSEMBLER BUILD_DIR
```

该脚本严格构建 `retrolsp`，然后用 [`tests/lsp_client.py`](../tests/lsp_client.py)
在标准输入输出上驱动服务器，把每个响应展开成 `key=value` 行并逐项断言。
场景文件位于 `tests/lsp/`：

| 场景 | 覆盖内容 |
| --- | --- |
| `arrays.json` | 握手与能力、诊断、符号树、悬浮、跳转、补全、签名帮助、未知方法 |
| `diagnostics.json` | 语法错误定位、`didChange` 后重新诊断、`didClose` 清理 |
| `utf16.json` | 中文注释的 UTF-16 位置换算 |
| `debounce.json` | 只输入不发请求时，防抖到期后自动推送诊断 |
| `robust.json` | 畸形头部块、非法 JSON 消息体、额外头部字段后仍能正常工作 |

`sh tests/run_tests.sh` 会自动包含这一步。用 sanitizer 运行时沿用与其他宿主工具
相同的 `LSP` 和 `RETROCC` 环境变量：

```sh
build_dir=$(mktemp -d)
cc -std=c11 -g -fsanitize=address,undefined -Wall -Wextra -Werror -Wpedantic \
   tools/retrolsp.c -o "$build_dir/retrolsp-sanitize"
cc -std=c11 -g -fsanitize=address,undefined -Wall -Wextra -Werror -Wpedantic \
   tools/retrocc.c -o "$build_dir/retrocc-sanitize"
cc -std=c11 -Wall -Wextra -Werror -Wpedantic tools/assembler.c -o "$build_dir/assembler"
ASAN_OPTIONS=halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 \
RETROCC="$build_dir/retrocc-sanitize" LSP="$build_dir/retrolsp-sanitize" \
   sh tests/test_lsp.sh "$build_dir/assembler" "$build_dir"
rm -rf "$build_dir"
```

## 维护

新增语言能力时，按 [`docs/compiler.md`](compiler.md) 的维护清单更新编译器，
并确认 [`--analyze`](compiler.md) 的 JSON 仍然包含语言服务器需要的字段。
如果新增了 token 种类或符号种类，需要同时更新本文的图例和 `tests/lsp/arrays.json`
中的断言。
