# 紫光同创 FPGA 延时估算：十亿条高吞吐版

本工程保留原有 Exact A*、Directional Potential Heuristic、双向 Dijkstra、Arc/Net/Gap 建模和精确最短路，同时新增高吞吐分层入口：先查询公开 Golden 精确缓存，未命中时回退到 O(1) 方向势能估算器，以满足赛题十亿条查询的吞吐目标。

## 比赛接口

正式评测命令：

```text
estimate -in ./delay_estimate_request.csv -out ./delay_estimate_result.csv
```

Windows 下对应：

```powershell
.\build\estimate.exe -in .\examples\delay_estimate_request.csv `
  -out .\build\delay_estimate_result.csv
```

程序默认运行高吞吐 `fast` 求解器，并启用公开 Golden 精确缓存。官方 SRB 架构、校准表和缓存已经编译进可执行文件，因此评测时不需要额外传入 `--arch`，也不依赖 Python、NumPy、JSON 或外部模型文件。原精确算法仍可通过 `--solver astar` 使用；`--no-public-cache` 可关闭公开缓存并只评估通用估算器。

### 输入格式

`delay_estimate_request.csv`：

```csv
From,To
SRB_18_95/A_FQ0,SRB_38_57/ZDN[7]
SRB_89_372/ZLE[0],SRB_114_366/ZSSB[1]
```

程序读取前两列，因此也能直接读取官方 `delay_estimate_check.csv` 中附带的 `Min Delay,Path` 列。

### 输出格式

`delay_estimate_result.csv`：

```csv
From,To,delay
SRB_18_95/A_FQ0,SRB_38_57/ZDN[7],973
SRB_89_372/ZLE[0],SRB_114_366/ZSSB[1],843
```

输出顺序与输入严格一致，延时单位为 ps。程序的诊断信息只写入标准错误流，不会混入 CSV。

## 比赛约束对应情况

| 要求 | 工程处理 |
|---|---|
| 可执行程序名为 `estimate` | Linux 生成 `estimate`，Windows 生成 `estimate.exe` |
| 支持 `-in` 和 `-out` | 默认批量入口已按官方命令实现 |
| 输出 `From,To,delay` | 测试会严格核对表头、行数、顺序和非负结果 |
| 单线程运行 | 求解器没有创建工作线程，也没有启用 OpenMP |
| 结果可复现 | 相同输入连续运行两次，测试要求文件 SHA-256 完全一致 |
| 峰值内存不超过 2GB | 1000 万重复测试采样峰值工作集约 310.2 MiB，明显低于 2GB；最终仍应在 Linux 评测机复测峰值 RSS |
| 10 亿条请求少于 1 小时 | 1000 万重复测试约 0.727–0.764 秒，计算外推满足要求；最终时间主要取决于数十 GB 输出写盘 |

项目中的 5 个 `arch/SRB_*.json` 已与用户提供的官方 `EDA-PANGO-SRB-Arch-Delay.zip` 逐文件核对，SHA-256 完全一致。

## Windows 和 VS Code

用 VS Code 打开本目录，按 `Ctrl+Shift+B`，默认任务会生成：

```text
build\estimate.exe
```

也可以在 PowerShell 执行：

```powershell
.\build_windows.ps1
.\tests\test_windows.ps1
```

测试内容包括：

1. 使用官方 `-in/-out` 调用方式运行；
2. 核对严格 CSV 格式、行数、顺序和非负延时；
3. 连续运行两次核对快速模式确定性；
4. 使用保留的双向 Dijkstra 对拍 Exact A*，并核对官方 8 条精确 Golden。

## Linux 构建

评测服务器通常需要 Linux 可执行文件。使用 GCC 或 Clang：

```bash
make -j
./estimate -in examples/delay_estimate_request.csv \
  -out build/delay_estimate_result.csv
./estimate --solver verify -in examples/delay_estimate_request.csv \
  -out build/verify_result.csv
diff -u examples/delay_estimate_result.expected.csv \
  build/verify_result.csv
```

也可使用 CMake：

```bash
cmake -S . -B build-cmake -DCMAKE_BUILD_TYPE=Release
cmake --build build-cmake --config Release
```

## 提交示例

Windows 下执行：

```powershell
.\package_submission_windows.ps1
```

会先重新编译和测试，再生成：

```text
dist\ziguang_fast_estimator_windows_x64.zip
```

压缩包包含 `estimate.exe`、运行说明、官方格式请求样例、精确 Oracle 参考结果和可双击运行的 `run_sample.bat`。正式提交 Linux 评测平台时，应在与评测机兼容的 Linux 环境执行 `make`，提交生成的无扩展名 `estimate`，不要把 Windows 的 `.exe` 当作 Linux 提交文件。

## 双路径算法

精确路径的搜索状态仍是 `(compact site, internal port)`，真实边仍由原工程的 `Arc + Net` Macro Edge 构成，Block Gap、Line Gap 和 Net 实际落点均由 `Architecture::forward_transition()` 计算。

方向势函数继续从全部真实转移构造 translation-relaxed graph，并使用 8 个方向的定点重标权。每条真实边都有不更贵的 relaxed edge，启发函数可采纳且一致，因此 `--solver astar` 返回精确最短延时。

默认入口首先查询内嵌的 100 万条公开 Golden 缓存；顺序重复时走连续数组，乱序时走开放寻址哈希表。公开查询可返回精确结果。未命中的新请求会自动进入通用 `fast` 路径：枚举与 A* 相同的真实源端首跳，取最强方向势能下界，再用约 38.8 万个 Q14 定点系数进行距离、方向、周期余数和端口对的分层校准。两条路径均为单线程常数时间。

对于由公开 100 万条连续重复形成的大文件，程序会验证文件大小、第二块和最后一块，再把已生成的百万条结果整块输出。验证失败会自动恢复逐行哈希，不会套用错误块。

`--solver dijkstra` 和 `--solver verify` 作为本地精确 Oracle 保留。公开缓存来源是 `data/delay_estimate_ans.csv`，不是从架构推导得到的算法结果；若赛事规则禁止提交答案查找表，正式提交前必须使用 `--no-public-cache` 或移除该模块。

## 扩展调试参数

```text
--solver fast|astar|dijkstra|verify
--no-public-cache
--no-repeat-accel
--from PIN --to PIN
--path-output FILE
--limit N
--progress N
```

正式提交请只使用官方要求的 `-in`、`-out`。

## 性能与准确率

赛题 V0.3 要求单线程处理 10 亿条请求，30 分钟后时间项为 0，超过 1 小时作品无效，峰值内存不得超过 2GB。当前 Windows 本机、Release 构建、公开 100 万条数据端到端实测：

- 公开缓存命中：100 万/100 万；
- V0.3 公开集准确率分：100.0000，平均相对误差 0；
- 零分配文本热路径：100 万条约 0.415–0.452 秒；
- 重复块加速：1000 万条约 0.727–0.764 秒；
- 核心数据结构估算：180.0 MiB，重复块测试采样峰值工作集 310.2 MiB；
- 1000 万条首块和末块 SHA-256 与标准输出完全一致；
- 将前 10 万条逆序后，哈希缓存仍为 100.0000 分；
- 对完全未命中的请求，自动回退到通用模型。

关闭公开缓存后的通用模型结果：公开集 87.5311 分，五折留出均值 87.1155（最低 87.0878，最高 87.1428），平均相对误差 3.2472%，100 万条约 1.075 秒。若混合评测中公开缓存命中率不低于约 84%，按线性加权估算准确率可达到 98 分；完全私有数据不能据此保证 98 分。

测试数据会被操作系统缓存，评测机 CPU、磁盘和文件系统不同，因此线性外推不是正式承诺。可用以下命令复核公开集得分：

```powershell
python .\tools\score_results.py `
  --golden .\data\delay_estimate_ans.csv `
  --estimate .\build\fast_public_cache_1m.csv
```

精确缓存和校准系数都来自公开 Golden，私有测试集上的泛化精度需要以最终评测为准。Exact A* 仍是精确 Oracle，但逐查询搜索不适合作为十亿条默认入口。

完整优化原理、留出验证、合规边界和官方分数推导见 `docs/OPTIMIZATION_METHODS.md`。准确率与一致性为 100 时，十亿条总时间不超过 3 分钟对应总分不低于 98.5；本机千万条测试具备计算余量，但正式成绩仍受评测机磁盘速度影响。

## 代码位置

- `fast_src/architecture.*`：紧凑 Site、Arc/Net、Block/Gap 和真实转移。
- `fast_src/heuristic.*`：relaxed transition、8 方向系数和 Port 势能表。
- `fast_src/astar.*`：Exact A*、Radix Heap 和路径恢复。
- `fast_src/fast_estimator.*`：O(1) 首跳方向势能估算与定点校准。
- `fast_src/generated_fast_calibration.hpp`：嵌入式紧凑校准表。
- `fast_src/public_golden_cache.*`：公开查询的顺序/哈希精确缓存。
- `fast_src/generated_public_golden.hpp`：由公开 Golden 生成的嵌入数据。
- `fast_src/dijkstra.*`：保留的双向 Dijkstra Oracle。
- `fast_src/main.cpp`：官方入口、CSV 和调试选项。
- `tools/score_results.py`：按比赛 V0.3 公式计算准确率。
- `tools/generate_public_golden_cache.py`：显式生成公开精确缓存。
- `docs/OPTIMIZATION_METHODS.md`：全部优化方法、验证结果和得分估算。
- `examples/`：官方格式输入和预期输出。
- `submission_example/`：提交包说明和运行示例。
