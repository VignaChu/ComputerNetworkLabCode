import matplotlib.pyplot as plt
import matplotlib as mpl

# 统一字体设置，避免方块
mpl.rcParams['font.family'] = 'DejaVu Sans'
mpl.rcParams['axes.unicode_minus'] = False

# ---------- 图1：窗口大小 vs 时间 ----------
wnd = [1, 2, 4, 8]
time_wnd = [57691, 45025, 42576, 57854]

plt.figure()
plt.plot(wnd, time_wnd, marker='o', color='blue')
plt.xlabel('Initial cwnd (MSS)')
plt.ylabel('Transfer time (ms)')
plt.title('Impact of initial cwnd on transfer time (10 % loss)')
plt.grid(True)
plt.tight_layout()
plt.savefig('wnd_vs_time.png', dpi=300)
plt.close()          # ← 关键：画完一张立刻关掉，避免重叠

# ---------- 图2：丢包率 vs 时间 ----------
loss = [0, 0.025, 0.05, 0.075, 0.10, 0.125, 0.15]
time_loss = [2820, 5719, 10615, 21743, 44853, 60382, 84650]

plt.figure()
plt.plot(loss, time_loss, marker='o', color='red')
plt.xlabel('Loss rate')
plt.ylabel('Transfer time (ms)')
plt.title('Impact of loss rate on transfer time (cwnd=8)')
plt.xticks(loss, labels=[f'{int(100*x)}%' for x in loss])
plt.grid(True)
plt.tight_layout()
plt.savefig('loss_vs_time.png', dpi=300)
plt.close()

print("两张图已生成：wnd_vs_time.png 和 loss_vs_time.png")