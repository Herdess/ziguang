紫光同创 FPGA 延时估算提交示例

正式评测调用：
  estimate -in ./delay_estimate_request.csv -out ./delay_estimate_result.csv

Windows 本地示例：
  estimate.exe -in delay_estimate_request.csv -out delay_estimate_result.csv

输入文件格式：
  第一行必须为 From,To
  每个数据行包含起点 Port 和终点 Port。

输出文件格式：
  第一行严格为 From,To,delay
  数据行顺序与输入保持一致，delay 单位为 ps。

estimate/estimate.exe 已内置官方 SRB 架构数据，运行时不需要 arch 目录。
Windows 包中的 generalization_model.srb 必须与 estimate.exe 放在同一目录；
Linux 正式构建会把模型直接嵌入 estimate。程序默认单线程，结果确定且可复现。

Linux 比赛构建不包含公开 Golden 答案表。默认使用 Directional Potential
快速物理估计与 800 棵 Huber 泛化模型；运行期缓存只保存本次进程
自行计算的预测，不包含训练答案。
如需精确最短路回归，可额外使用 --solver astar；
如需同时核对 Exact A* 与双向 Dijkstra，可使用 --solver verify。

delay_estimate_result.expected.csv 是 8 条样例的精确 Oracle 结果，
用于 --solver astar/verify 回归；默认快速估算结果允许与它有误差。
