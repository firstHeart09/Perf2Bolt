# 切换到测试目录
cd $HOME/study/bolt/test
# 使用perf2bolt生成文件
perf2bolt -p perf.data -o perf.fdata ./reorder > branch_events.log
# 查看log文件
vim branch_events.log
