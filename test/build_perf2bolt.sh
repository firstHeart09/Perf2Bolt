# 切换到测试目录
cd $HOME/study/Perf2Bolt/test
# 使用perf2bolt生成文件
perf2bolt -p /home/dushuai/study/Perf2Bolt/perf.data -o /home/dushuai/study/Perf2Bolt/perf.fdata /home/dushuai/study/Perf2Bolt/reorder
# 查看log文件
# vim branch_events.log
