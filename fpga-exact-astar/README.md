# 紫光 FPGA Exact A* + Directional Potential Heuristic

这是在原 Exact Golden C++ 工程上新增的精确 A* 求解器。原有紧凑状态、真实 Arc/Net/Gap 转移、双向 Dijkstra、Radix Heap 和路径恢复均保留；A* 使用基于全部真实转移构造的 8 方向势函数，因此结果不是近似值。

## 算法结构

搜索状态是 `(compact site, internal port)`。一条搜索边仍是原工程的 `Arc + Net` Macro Edge，Net 的实际落点和 Block/Gap Line 延时直接复用 `Architecture::forward_transition()`。

启动时新增两步预计算：

1. 遍历每种 Net 在全部 Site 上的真实落点，把相同 `(from internal, to internal, dx, dy)` 的延时取最小值，得到更自由、但不会更贵的 relaxed graph。
2. 对右、左、上、下和四个对角方向计算定点重标权，并在 160 个 Internal Port 组成的小图上计算到 496 种 Target Port 的势能。

方向 `u` 的整数系数满足：

```text
Q = 256
s = min floor(Q * edge_cost / dot(u, edge_displacement))
(ax, ay) = s * u
reduced_cost = Q * edge_cost - ax * dx - ay * dy >= 0
```

查询时使用：

```text
H(n,T) = max_k ceil((ax[k] * (Tx-Nx) + ay[k] * (Ty-Ny)
                         + Potential[k][target_port][internal_port]) / 256)
```

每条真实边都有一条不更贵的 relaxed edge，且重标权非负，所以 `H` 可采纳并满足一致性。A* 的 `min(open.f) >= best` 终止条件返回精确最短延时。

## Windows / VS Code 构建

用 VS Code 打开本目录，然后按 `Ctrl+Shift+B`。构建脚本优先使用 PATH 中的 MinGW-w64 `g++`；如果本机没有编译器，会下载并校验 Zig 官方 Windows 工具链，放到本项目的 `.tools` 目录。

也可以在 PowerShell 中运行：

```powershell
.\build_windows.ps1
```

生成文件：

```text
build\exact_astar.exe
```

如果已经安装 CMake，也可以使用标准 CMake 流程：

```powershell
cmake -S . -B build-cmake -DCMAKE_BUILD_TYPE=Release
cmake --build build-cmake --config Release
```

## 运行

单条查询（同时输出路径）：

```powershell
.\build\exact_astar.exe --solver astar `
  --from "SRB_84_356/ZLE[0]" `
  --to "SRB_46_116/ZSSB[6]"
```

批量查询：

```powershell
.\build\exact_astar.exe --solver astar `
  --input tests\golden_sample.csv `
  --output build\astar_result.csv `
  --path-output build\astar_paths.csv
```

可用求解模式：

- `--solver astar`：Exact A* + Directional Potential Heuristic，默认模式。
- `--solver dijkstra`：保留的原双向 Dijkstra。
- `--solver verify`：每条查询同时运行两种算法，结果不同立即报错。

## 测试

在 VS Code 运行任务 `Test Exact A* vs Golden`，或执行：

```powershell
.\tests\test_windows.ps1
```

测试会核对三份结果：新增 A*、原双向 Dijkstra、仓库内的 8 条 Golden 样例。成功时输出：

```text
PASS: Exact A*, bidirectional Dijkstra, and 8-row Golden CSV are identical.
```

本机 Release 实测：

| 数据 | Exact A* | 双向 Dijkstra |
|---|---:|---:|
| 8 条相同查询耗时 | 4.33 s | 7.57 s |
| settled states | 7,821,910 | 25,468,483（双向合计） |
| relaxed edges | 17,420,162 | 43,350,091 |

此外，Exact A* 已对已有 Golden CSV 的前 100 条查询完成核对，`mismatch = 0`。计时会随电脑负载变化，应主要比较同一次运行中的相对结果。

## 代码位置

- `fast_src/architecture.*`：紧凑 Site、Arc/Net、Block/Gap 和真实转移。
- `fast_src/heuristic.*`：relaxed transition、8 方向系数、Port 势能表。
- `fast_src/astar.*`：Exact A*、Radix Heap、路径恢复。
- `fast_src/dijkstra.*`：原双向 Dijkstra Oracle。
- `fast_src/main.cpp`：命令行、CSV、三种求解模式和统计。

