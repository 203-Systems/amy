# AMY 合成器库

若本仓位于 203 工作区，先读取上级目录的 `../AGENTS.md`，再按本文件和仓内维护文档处理项目细节。

`src/` 是跨平台 C 合成核心与公开 API，`amy/` 是 Python 接口和音色工具，`godot/` 与 `docs/` 提供 Godot 及浏览器集成。改动前按对应平台阅读项目的发布与生成规则。[README](README.md) · [维护说明](CLAUDE.md)

- 公共入口由 `src/amy.h` 声明，渲染跟到 `src/amy.c`，采样数据的注册与回收跟到 `src/pcm.c`。`pcm_load_external` 使用调用方持有的交错帧，调用方须保持数据有效且不变直到卸载。
- 保持 C API、平台绑定和现有音频行为兼容。DSP 改动关注定点计算、采样率、声道、实时内存以及现有音频回归参考。调整包络或 FM 算法时可对照[包络曲线](docs/envelope.png)及[DX7 算法图](docs/dx7_algorithms.jpg)。
- 在 `amy/__init__.py`、`src/amy.h` 或音色／PCM 源数据更改后，按涉及的接口使用 `make c-api`、`make godot-api` 更新生成件，并用 `make check-c-api` 检查。WebAssembly 的 AMY 构建和 MicroPython REPL 各有发布流程。
- 从 `Makefile` 与对应工作流选测试。核心改动按需用 `make ctest` 或 `make test`；Godot 改动需要核对 `godot/` 及 `setup_godot.sh` 的本地路线。版本和发行产物由现有 release 工作流管理。[构建与检查](Makefile) · [发行工作流](.github/workflows/release.yml)
