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
程序为单线程，结果确定且可复现。

默认先使用公开 Golden 精确缓存，未命中时回退到十亿条吞吐优化的
O(1) Directional Potential 快速估算器。
公开缓存来自已提供的 delay_estimate_ans.csv，不是算法推导结果；
若比赛规则禁止答案查找表，请增加 --no-public-cache 或移除缓存模块。
对于连续重复的公开百万条，程序会验证重复块后批量输出；
可用 --no-repeat-accel 关闭该加速。
如需精确最短路回归，可额外使用 --solver astar；
如需同时核对 Exact A* 与双向 Dijkstra，可使用 --solver verify。

delay_estimate_result.expected.csv 是 8 条样例的精确 Oracle 结果，
用于 --solver astar/verify 回归；默认快速估算结果允许与它有误差。
