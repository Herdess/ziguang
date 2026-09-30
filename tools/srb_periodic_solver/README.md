# 周期 Golden 在线 Solver

## 2026-09-30 残差修正版

当前源码在 208-source、4×8、16-bit 周期网格基础上增加了稀疏结构残差修正。修正规则只使用
`(原始 source port, 原始 target port, dx, dy)`，不保存具体 `From,To` 请求，也不进行在线搜索。

规则由合并 Golden 中后 8,483,192 条生成，前 1,000,000 条完整留作验证。精确重合检查结果为：

```text
holdout_unique=1000000
train_rows=8483192
overlap_rows=0
overlap_unique=0
```

验证数据 SHA-256：

```text
公开 1,000,000 条：2D3350B1AED4B7489FBD2FC361B2D680A422DB218159A07938782ED850B4C19F
合并 9,483,192 条：2F0FD0D7EB467F904B3618631587F32260E33DAAF1E27DCACD1357364281919D
```

本机留出验证结果如下。这里的分数采用项目内 Competition V0.3 公式计算，不是官方平台得分：

| 版本 | accuracy | MAPE | 精确命中 | 100 万条覆盖 |
| --- | ---: | ---: | ---: | ---: |
| 原始周期网格 | 97.227735 | 0.715573% | 319,908 | 100% |
| 网格 + 稀疏残差（禁用 Gap Block 修正） | 98.069856 | 0.493748% | 408,059 | 100% |

残差规则生成条件为：绝对残差至少 10 ps、同一结构键至少出现 5 次、训练残差跨度不超过
10 ps，共生成 136,403 条规则。规则对含 Gap Block 增量的候选路径禁用，因为这类误差依赖绝对
位置，不满足平移不变假设。

风险说明：上述 98.069856 是本地独立请求留出结果，不能等同于官方隐藏集得分；Linux 比赛二进制
仍需在 Linux/CI 环境重新编译并进行提交前验证。模型文件和师兄方案也应在团队授权及比赛规则允许
的前提下使用。

## 快速使用

本目录的 `data/grid208_4x8_u16.bin` 是指向共享模型文件的符号链接。链接就绪后，先跑 1000 行快速复现：

```bash
./reproduce.sh
```

默认输入为：

```text
../EDA-PANGO-SRB-Arch-Delay/data/delay_estimate_ans.csv
```

完整测试：

```bash
./run.sh \
  ../EDA-PANGO-SRB-Arch-Delay/data/delay_estimate_ans.csv \
  ./output/result.csv
```

也可以指定模型或限制行数：

```bash
MODEL=/path/to/grid208_4x8_u16.bin LIMIT=10000 ./run.sh input.csv output.csv
```

程序支持的输入选项由二进制程序直接提供：`--input FILE`、`--model FILE`、`--output FILE`、`--limit N`、`--progress N`、`--no-line-est`、`--always-fused-logic`。发布脚本默认开启 `--always-fused-logic`，这是当前 208 源模型的最快查询路径。

比赛接口同时支持：

```text
estimate -in <input_file> -out <output_file>
```

二列表头 `From,To` 会输出严格的 `From,To,delay`；带 Golden 的三列输入会输出诊断列，用于离线评分。

Windows 下可以直接构建和运行：

```powershell
.\build_windows.ps1
.\run_windows.ps1 `
  -InputFile .\input.csv `
  -OutputFile .\output.csv `
  -ModelFile C:\path\to\grid208_4x8_u16.bin
```

重新生成并检查残差规则：

```powershell
python .\generate_residual_header.py `
  --train .\merged_result.csv `
  --arch-header .\src\generated_arch_data.hpp `
  --output .\src\generated_residual_corrections.hpp `
  --skip-train-rows 1000000 `
  --threshold 10 --minimum-count 5 --maximum-spread 10

python .\check_holdout_overlap.py .\merged_golden.csv --holdout-rows 1000000
python .\analyze_results.py .\validation_result.csv
```

## Solver 原理

程序逐行读取 `from,to,delay` CSV，只解析 from/to pin，并将端口归一化为模型支持的 source/target。它根据两个 SRB 的相对位移计算周期商与余数，再从 memory-mapped 的 16-bit 4×8 模型中读取 delay；必要时再加上 gap line 的估计增量。Logic source 的候选组合已离线融合进 208-source 模型，在线不再枚举所有 arc+net 组合。

模型采用 `mmap` 只读映射。文件内容由操作系统按访问页加载，进程不会把 1.72 GiB 全部复制到 C++ heap；但访问覆盖面很大时 RSS 仍可能逐步接近工作集大小。

## 模型链接和发布

`data/grid208_4x8_u16.bin` 不应提交到普通 Git 仓库。它应链接到团队共享目录中的同名文件，并在共享目录保存 SHA-256。源码包内已包含架构头文件，因此编译 solver 不再读取五个 JSON。若换机器，只需要重新建立 `data/grid208_4x8_u16.bin` 链接或通过 `MODEL=...` 指定模型。

本次验证所用模型的 SHA-256 为：

```text
AD3CCA6C68B2F50DA5F2534ADDD9B2DBB5ECA10456717FBDC6AD06AC97B70B1C
```

当前模型是基于 free Golden 的在线压缩版本；gap block/line 的补偿方式和误差边界见项目的 `tools/srb_translation_invariant/doc/` 文档。
