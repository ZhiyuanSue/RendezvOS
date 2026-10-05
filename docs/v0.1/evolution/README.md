# evolution（规划）

待办、已关闭项、尚未实现的 design。这里的内容不是 v0.1 现行契约。

计划文件：

```
v0.1/evolution/
├── README.md
├── TODO.md
├── ROCm.md                 本机 GPU 直通 / ROCm 阅读笔记（远期）
├── archive/已完成与关闭项.md
└── design/
    ├── IRQ亲和性与IOAPIC.md
    ├── 线程运行期迁移与affinity_mask.md
    ├── lockfree-IPC远期.md
    └── 日志与console-server.md
```

进度：full 初稿 44 篇已完成。按操作计划做 **逐篇深读代码重做**（进度见 [`../full重做进度.md`](../full重做进度.md)）；`TODO.md` 含远期代码项 E1–E11；`ROCm.md` 已从 `old/` 迁入；`archive/`、`design/*.md` 仍仅规划。
