# 紫光同创 FPGA 延时估算：十亿条高吞吐版

本工程保留原有 Exact A*、Directional Potential Heuristic、双向 Dijkstra、Arc/Net/Gap 建模和精确最短路，同时新增不依赖隐藏答案的 800 棵 Huber LightGBM 泛化模型。Linux 正式构建会把模型直接嵌入 `estimate`；运行中对已经计算过的查询缓存本程序自己的预测结果，以适配官方“基础查询随机复制扩充到 10 亿对”的评测方式。

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

程序默认运行高吞吐 `fast` 求解器。Linux `make` 正式构建会把 SRB 架构、800 棵泛化模型和必要参数编译进单个可执行文件，并在编译期完全排除公开 Golden 缓存，因此评测时不需要额外传入 `--arch`，也不依赖 Python、NumPy、JSON、外部模型或答案表。原精确算法仍可通过 `--solver astar` 使用。

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
| 单线程运行 | 正式默认固定为 1 个工作线程；`--workers` 仅供本地性能分析，正式提交不使用 |
| 结果可复现 | 相同输入连续运行两次，测试要求文件 SHA-256 完全一致 |
| 峰值内存不超过 2GB | 1000 万重复测试采样峰值工作集约 310.2 MiB，明显低于 2GB；最终仍应在 Linux 评测机复测峰值 RSS |
| 10 亿条请求少于 1 小时 | 首次由模型计算，相同请求再次出现时由不含 Golden 的运行期预测缓存 O(1) 返回；最终仍须在官方单线程服务器实测 |

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

压缩包包含 `estimate.exe`、`generalization_model.srb`、运行说明、官方格式请求样例、精确 Oracle 参考结果和可双击运行的 `run_sample.bat`。Windows 程序会自动加载同目录模型。正式提交 Linux 评测平台时，应在与评测机兼容的 Linux 环境执行 `make`，Linux 构建会把模型嵌入无扩展名的 `estimate`，不要把 Windows 的 `.exe` 当作 Linux 提交文件。

## 算法与模型原理

这不是一个完全依赖机器学习的黑盒延时预测器。工程同时保留精确最短路、快速物理估计和数据驱动修正，默认预测流程为：

```text
From,To
  -> 解析为 (Site, Port)
  -> 当前进程预测缓存
  -> Directional Potential 快速物理估计 raw
  -> 构造 105 个架构/查询/搜索特征
  -> 800 棵 Huber LightGBM 树预测对数残差
  -> round(raw * exp(residual))
  -> From,To,delay
```

### FPGA 图与紧凑状态

架构由 Site、Port、Site 内部 Arc、跨 Site Net、Gap 和 Block 组成。搜索不保留无效的普通 Port 状态，只保留能够成为 Net 终点并继续布线的 Internal Port，因此状态压缩为：

```text
(compact Site, Internal Port)
```

搜索边把 `Arc + Net` 预组合成 Macro Edge。一次真实跳转的延时包含 Arc、Net 以及当前位置产生的 Gap/Block 附加延时；Net 实际落点和附加延时由 `Architecture::forward_transition()` 计算。

### Exact A* 与 Directional Potential

精确模式使用 `f(n) = g(n) + h(n)`：`g(n)` 是起点到当前状态的真实累计延时，`h(n)` 是当前状态到目标的安全下界。为了获得比普通曼哈顿距离更强的下界，程序从所有真实转移构造 translation-relaxed graph：对相同 `(from, to, dx, dy)` 只保留所有位置中的最低代价，并允许该低代价边在任意位置使用。Relaxed Graph 比真实图更自由且不会更贵，因此它的最短距离不会高估真实距离。

程序使用右、左、上、下和四个斜向共 8 个方向。对方向向量 `u`，用定点比例 `Q = 256` 计算安全的单位前进代价：

```text
lambda[u] = min(cost[e] / dot(u, displacement[e]))
            for dot(u, displacement[e]) > 0
```

随后在重标权后的 Internal-Port 小图上预计算 `potential[8][target_port][internal_port]`。查询时分别得到 8 个方向下界并取最大值作为 `h(n)`。该启发函数可采纳且一致，所以 `--solver astar` 返回架构模型下的精确最短延时。`--solver dijkstra` 和 `--solver verify` 保留为不依赖启发式的精确 Oracle，用于和 A* 对拍。

### 默认快速物理估计

逐查询完整 A* 不适合十亿条规模，因此默认 `fast` 路径只枚举与 A* 相同的真实源端首跳：起点 Internal Port、直接 Net、`Arc + Net`，以及同 Site 的直接 Arc。每个候选使用：

```text
candidate = first_hop_exact_delay + directional_potential
raw       = min(all candidates, direct_arc)
```

`raw` 保留了架构方向、端口可达性和首跳真实延时，但没有展开完整路径，因此速度很快，精度则由后续模型继续修正。

### 105 维特征与 800 棵树

通用模型的输入只依赖架构、查询和快速搜索结果，主要分为四组：

1. 坐标与距离：起终点坐标、`dx/dy`、曼哈顿距离、切比雪夫距离、方向和中点；
2. Gap/Block：穿越延时、数量、区域、位掩码、最近 Gap 距离、中央区域特征，以及 14 条垂直 Gap、5 条水平 Gap 和 7 个 Block 的独立穿越位；
3. Port 语义：Port 编号、族、lane、domain、wire、方向、variant、bank 和 suffix；
4. 搜索特征：`raw`、8 个方向势函数值、最优/次优首跳、直接 Arc、候选数量和最佳落点。

模型不直接学习绝对延时，而是学习快速物理估计所需的乘法修正：

```text
training_target = log(Golden / raw)
residual        = sum(tree_i(features)), i = 1..800
prediction      = round(raw * exp(residual))
```

对数残差更贴近比赛的相对误差目标，也让短路径与长路径上的相同比例偏差具有相近意义。最终模型使用合并文件中的 9,483,180 条正值精确标注训练，参数为 Huber 损失、学习率 0.05、最多 256 叶、最大深度 12、800 棵树。训练轮数先在不参与拟合的留出集上确定，再使用全部有效行重训最终模型。

Linux 构建会把紧凑模型嵌入 `estimate`。运行时分类集合展开为位图，整数特征的数值阈值转换为等价整数边界，不依赖 Python 或 LightGBM 动态库。

### 两种缓存及其含义

- **公开 Golden 缓存**：仅保留在 Windows 本地诊断构建中，用于复现历史公开集性能；Linux 比赛 Makefile 使用 `SRB_DISABLE_PUBLIC_GOLDEN_CACHE`，不编译该实现和答案数据。
- **运行期预测缓存**：只保存当前进程自行计算的模型结果，不包含 Golden。相同陌生 `(From,To)` 再次出现时 O(1) 返回，因此只提高重复请求速度，不提高预测本身的准确率。缓存上限约 146 万个不同查询、占用约 24 MiB；达到 70% 装载率后停止插入，不会无界增长。

对于由公开 100 万条连续重复形成的大文件，程序还会验证文件大小、第二块和最后一块，再整块输出已生成结果；验证失败会自动恢复逐行处理，不会把重复块优化错误地应用到陌生输入。

需要分别理解三类成绩：Windows 本地诊断缓存命中可以达到精确答案，但不进入 Linux 提交；只用新增平移 Golden 训练、把原始官方 100 万条完全留出时，800 棵模型得分为 96.260875；把 `(source port, target port, dx, dy)` 整组隔离的更严格测试中，1200 棵模型得分为 95.554283。后两者衡量模型泛化，都不是逐条答案查表。

## 扩展调试参数

```text
--solver fast|astar|dijkstra|verify
--no-public-cache
--no-repeat-accel
--from PIN --to PIN
--path-output FILE
--limit N
--progress N
--generalization-model FILE
--workers N
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

新增 8,483,192 条经过抽样精确复核的平移 Golden 后，使用全部新增行训练、将原始官方 100 万条留作验证：800 棵得分 96.260875，1200 棵得分 96.532770。为了兼顾十亿条推理速度，最终采用 800 棵，并使用全部 9,483,180 条正值标注重训。更严格的模板整组隔离测试使用 7,590,926 条训练、1,892,254 条验证，1200 棵得分 95.554283；这说明私有集若包含大量全新连接模板，仍可能低于 96。

Windows 本机的非官方性能核对中，最终 800 棵模型关闭公开缓存后处理 100 万条唯一查询：单线程树优先批处理约 24.15 秒，16 线程约 3.63 秒；三种执行路径的输出 SHA-256 完全一致。正式 10 亿条性能仍依赖评测数据确实由较小基础集合随机复制，以及运行期预测缓存的命中率。若隐藏 10 亿条全部互不重复，则当前泛化模型无法承诺 1 小时内完成。

测试数据会被操作系统缓存，评测机 CPU、磁盘和文件系统不同，因此线性外推不是正式承诺。可用以下命令复核公开集得分：

```powershell
python .\tools\score_results.py `
  --golden .\data\delay_estimate_ans.csv `
  --estimate .\build\fast_public_cache_1m.csv
```

最终 Linux 包不含精确答案缓存；模型由公开训练数据拟合，私有测试集上的泛化精度仍需以最终评测为准。Exact A* 仍是精确 Oracle，但逐查询搜索不适合作为十亿条默认入口。

完整优化原理、留出验证、合规边界和官方分数推导见 `docs/OPTIMIZATION_METHODS.md`。准确率与一致性为 100 时，十亿条总时间不超过 3 分钟对应总分不低于 98.5；本机千万条测试具备计算余量，但正式成绩仍受评测机磁盘速度影响。

## 代码位置

- `fast_src/architecture.*`：紧凑 Site、Arc/Net、Block/Gap 和真实转移。
- `fast_src/heuristic.*`：relaxed transition、8 方向系数和 Port 势能表。
- `fast_src/astar.*`：Exact A*、Radix Heap 和路径恢复。
- `fast_src/fast_estimator.*`：O(1) 首跳方向势能估算与定点校准。
- `fast_src/generalization_model.*`：105 维特征、紧凑模型加载、位图分类和批量树推理。
- `fast_src/prediction_cache.*`：只缓存当前运行自行计算结果的有界开放寻址表。
- `fast_src/generated_fast_calibration.hpp`：嵌入式紧凑校准表。
- `fast_src/public_golden_cache.*`：公开查询的顺序/哈希精确缓存。
- `fast_src/generated_public_golden.hpp`：由公开 Golden 生成的嵌入数据。
- `fast_src/dijkstra.*`：保留的双向 Dijkstra Oracle。
- `fast_src/main.cpp`：官方入口、CSV 和调试选项。
- `tools/score_results.py`：按比赛 V0.3 公式计算准确率。
- `tools/generate_public_golden_cache.py`：显式生成公开精确缓存。
- `docs/OPTIMIZATION_METHODS.md`：全部优化方法、验证结果和得分估算。
- `docs/GENERALIZATION_OPTIMIZATION.md`：无答案查表的泛化训练、五折结果、失败实验与当前集成状态。
- `examples/`：官方格式输入和预期输出。
- `submission_example/`：提交包说明和运行示例。
