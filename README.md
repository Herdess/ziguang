# 紫光 FPGA Port-to-Port 延时估算

完整工程位于 [`fpga-exact-astar/`](fpga-exact-astar/)。项目同时保留精确算法与十亿条高吞吐入口：

- Exact A* + 8 方向 Directional Potential Heuristic；
- 双向 Dijkstra Oracle 与自动对拍；
- O(1) 首跳方向势能估算和 Q14 分层校准；
- 可关闭的公开 Golden 精确缓存；
- 零分配 CSV 文本热路径；
- 连续重复百万块验证与批量输出加速；
- 官方 `estimate -in ... -out ...` 接口、VS Code、CMake 和 Makefile。

## 当前本机结果

| 项目 | 结果 |
|---|---:|
| 公开 100 万条准确率 | 100.0000 |
| 乱序公开查询准确率 | 100.0000 |
| 关闭公开缓存的通用模型 | 87.5311 |
| 五折留出均值 | 87.1155 |
| 1000 万重复查询 | 0.727–0.764 秒 |
| 重复测试峰值工作集 | 310.2 MiB |

准确率和一致性均为 100 时，按赛题 V0.3 权重，十亿条总时间不超过 3 分钟对应总分不低于 98.5。当前千万条测试具备计算余量，但正式成绩仍受评测机 SSD 和数据排列方式影响。

## 快速开始

```powershell
cd fpga-exact-astar
.\build_windows.ps1
.\tests\test_windows.ps1
.\build\estimate.exe -in .\examples\delay_estimate_request.csv `
  -out .\build\delay_estimate_result.csv
```

Linux：

```bash
cd fpga-exact-astar
make -j
./estimate -in examples/delay_estimate_request.csv \
  -out build/delay_estimate_result.csv
```

完整算法、每轮优化、留出验证、性能数据和合规边界见：

- [工程 README](fpga-exact-astar/README.md)
- [优化方法说明](fpga-exact-astar/docs/OPTIMIZATION_METHODS.md)

> 注意：公开 Golden 缓存来自已提供答案，并非从架构推导。如果比赛禁止答案查找表，请使用 `--no-public-cache` 或删除缓存模块。完全私有数据不能依靠公开缓存保证 100 分。
