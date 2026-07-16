import ctypes
import ctypes.wintypes
import os
import sys
import threading
import time
import tkinter as tk
from tkinter import filedialog, messagebox, ttk

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
BUILD_DIR  = os.path.join(SCRIPT_DIR, "build")

# 让 Windows 能找到 DLL 依赖（libwinpthread-1.dll 等）
if hasattr(os, "add_dll_directory"):
    os.add_dll_directory(BUILD_DIR)
    for candidate in [r"C:\msys64\mingw64\bin", r"C:\mingw64\bin"]:
        if os.path.isdir(candidate):
            os.add_dll_directory(candidate)

DLL_PATH = os.path.join(BUILD_DIR, "libFileScanner.dll")
dll = ctypes.CDLL(DLL_PATH)

dll.CreateFileScannerEx.argtypes = [
    ctypes.c_void_p, ctypes.c_void_p, ctypes.c_void_p,
]
dll.CreateFileScannerEx.restype = ctypes.c_void_p

dll.DestroyFileScanner.argtypes = [ctypes.c_void_p]
dll.Scanner_StartScan.argtypes  = [ctypes.c_void_p, ctypes.c_wchar_p]
dll.Scanner_StartScan.restype   = ctypes.c_int
dll.Scanner_StopScan.argtypes   = [ctypes.c_void_p]
dll.Scanner_IsScanning.argtypes = [ctypes.c_void_p]
dll.Scanner_IsScanning.restype  = ctypes.c_int

CB_FILE  = ctypes.CFUNCTYPE(None, ctypes.c_wchar_p, ctypes.c_ulonglong, ctypes.c_ulonglong)
CB_ERROR = ctypes.CFUNCTYPE(None, ctypes.c_wchar_p, ctypes.c_wchar_p)
CB_DONE  = ctypes.CFUNCTYPE(None, ctypes.c_int)


class ScannerApp(tk.Tk):
    def __init__(self):
        super().__init__()
        self.title("FileScanner")
        self.geometry("900x600")
        self.minsize(600, 400)

        self.scanner    = None
        self.scanning   = False
        self.file_count = 0
        self.total_size = 0

        # 双映射：路径->iid（插入时查找父节点），iid->路径（双击打开文件）
        self._node_map     = {}
        self._iid_to_path  = {}

        # 保持对回调 ctypes 对象的引用，防止被 GC 回收导致 DLL 回调时 crash
        self._cb_file  = CB_FILE(self._on_file_found)
        self._cb_error = CB_ERROR(self._on_error)
        self._cb_done  = CB_DONE(self._on_scan_complete)

        self._build_toolbar()
        self._build_tree()
        self._build_statusbar()

        self.protocol("WM_DELETE_WINDOW", self._on_close)

    # ---- 布局 ----

    def _build_toolbar(self):
        bar = ttk.Frame(self)
        bar.pack(fill=tk.X, padx=8, pady=(8, 4))

        ttk.Label(bar, text="文件夹:").pack(side=tk.LEFT)

        self.path_var = tk.StringVar(value=os.path.expanduser("~"))
        entry = ttk.Entry(bar, textvariable=self.path_var)
        entry.pack(side=tk.LEFT, fill=tk.X, expand=True, padx=(4, 4))
        entry.bind("<Return>", lambda e: self._start_scan())

        ttk.Button(bar, text="浏览...", command=self._browse).pack(side=tk.LEFT, padx=2)
        self.btn_start = ttk.Button(bar, text="开始扫描", command=self._start_scan)
        self.btn_start.pack(side=tk.LEFT, padx=2)
        self.btn_stop = ttk.Button(bar, text="停止", command=self._stop_scan,
                                   state=tk.DISABLED)
        self.btn_stop.pack(side=tk.LEFT, padx=2)

    def _build_tree(self):
        frame = ttk.Frame(self)
        frame.pack(fill=tk.BOTH, expand=True, padx=8, pady=4)

        self.tree = ttk.Treeview(frame, columns=("size", "mtime"),
                                 show="tree headings", selectmode="browse")
        self.tree.heading("#0", text="名称")
        self.tree.heading("size", text="大小")
        self.tree.heading("mtime", text="修改时间")
        self.tree.column("#0", width=450, minwidth=200)
        self.tree.column("size", width=100, minwidth=80, anchor=tk.E)
        self.tree.column("mtime", width=130, minwidth=100, anchor=tk.CENTER)

        vsb = ttk.Scrollbar(frame, orient=tk.VERTICAL, command=self.tree.yview)
        self.tree.configure(yscrollcommand=vsb.set)
        self.tree.bind("<Double-1>", self._on_double_click)

        self.tree.grid(row=0, column=0, sticky="nsew")
        vsb.grid(row=0, column=1, sticky="ns")
        frame.grid_rowconfigure(0, weight=1)
        frame.grid_columnconfigure(0, weight=1)

    def _build_statusbar(self):
        bar = ttk.Frame(self)
        bar.pack(fill=tk.X, padx=8, pady=(4, 8))

        self.status_var = tk.StringVar(value="就绪")
        ttk.Label(bar, textvariable=self.status_var).pack(side=tk.LEFT)

        self.stat_var = tk.StringVar()
        ttk.Label(bar, textvariable=self.stat_var).pack(side=tk.RIGHT)

        self.progress = ttk.Progressbar(bar, mode="indeterminate")
        self.progress.pack(side=tk.RIGHT, padx=(0, 12))

    # ---- 按钮事件 ----

    def _browse(self):
        path = filedialog.askdirectory(initialdir=self.path_var.get())
        if path:
            self.path_var.set(path)

    def _start_scan(self):
        path = self.path_var.get().strip()
        if not path:
            messagebox.showwarning("提示", "请先选择文件夹")
            return
        if not os.path.isdir(path):
            messagebox.showerror("错误", f"路径不存在或不是目录:\n{path}")
            return

        self.tree.delete(*self.tree.get_children())
        self._node_map.clear()
        self.file_count = 0
        self.total_size = 0

        # 计时由 UI 层负责——DLL 不应关心界面展示需求
        self._scan_start_time = time.time()

        self.scanner = dll.CreateFileScannerEx(
            self._cb_file, self._cb_error, self._cb_done)
        ok = dll.Scanner_StartScan(self.scanner, path)
        if not ok:
            messagebox.showerror("错误", "无法启动扫描（路径无效或正在扫描）")
            dll.DestroyFileScanner(self.scanner)
            self.scanner = None
            return

        self.scanning = True
        self.btn_start.config(state=tk.DISABLED)
        self.btn_stop.config(state=tk.NORMAL)
        self.status_var.set("正在扫描...")
        self.stat_var.set("")
        self.progress.start(15)

        # 每 100ms 轮询 DLL 的 IsScanning
        self._poll_scan()

    def _stop_scan(self):
        if self.scanner:
            dll.Scanner_StopScan(self.scanner)
        self._on_scan_finished(cancelled_by_user=True)

    def _poll_scan(self):
        if not self.scanning:
            return
        if dll.Scanner_IsScanning(self.scanner):
            self.after(100, self._poll_scan)
        else:
            self._on_scan_finished(cancelled_by_user=False)

    def _on_scan_finished(self, cancelled_by_user):
        self.scanning = False
        self.progress.stop()
        self.btn_start.config(state=tk.NORMAL)
        self.btn_stop.config(state=tk.DISABLED)

        if cancelled_by_user:
            self.status_var.set("扫描已取消")
        else:
            elapsed = time.time() - self._scan_start_time
            self.status_var.set(f"扫描完成（耗时 {self._fmt_elapsed(elapsed)}）")
        self._update_stats()

        if self.scanner:
            dll.DestroyFileScanner(self.scanner)
            self.scanner = None

    def _on_double_click(self, event):
        sel = self.tree.selection()
        if not sel:
            return
        path = self._iid_to_path.get(sel[0], "")
        if path and os.path.isfile(path):
            os.startfile(path)

    def _on_close(self):
        if self.scanning:
            dll.Scanner_StopScan(self.scanner)
        if self.scanner:
            dll.DestroyFileScanner(self.scanner)
        self.destroy()

    # ---- C 回调（运行在 DLL 工作线程，必须用 after 抛回主线程） ----

    def _on_file_found(self, path, size, last_write_time):
        self.after(0, self._add_file_to_tree, path, size, last_write_time)

    def _on_error(self, path, msg):
        self.after(0, lambda: self.status_var.set(f"跳过: {path}"))

    def _on_scan_complete(self, cancelled):
        self.after(0, lambda: self.status_var.set("扫描完成（DLL 通知）"))

    # ---- 树操作（主线程） ----

    def _add_file_to_tree(self, path, size, last_write_time):
        self.file_count += 1
        self.total_size += size

        time_str = self._filetime_to_str(last_write_time)
        parts = path.split("\\")
        if not parts:
            return

        for depth in range(1, len(parts) + 1):
            ancestor = "\\".join(parts[:depth])
            if ancestor not in self._node_map:
                parent_path = "\\".join(parts[:depth - 1])
                parent_iid = self._node_map.get(parent_path, "")
                is_file = (depth == len(parts))

                iid = self.tree.insert(
                    parent_iid, "end",
                    text=parts[depth - 1],
                    values=(
                        self._fmt_size(size) if is_file else "",
                        time_str if is_file else ""),
                    open=False)
                self._node_map[ancestor] = iid
                self._iid_to_path[iid] = ancestor

        # 每 50 个文件刷新一次统计标签，避免频繁更新拖慢 GUI
        if self.file_count % 50 == 0:
            self._update_stats()

    def _update_stats(self):
        self.stat_var.set(
            f"文件: {self.file_count:,}  |  "
            f"总大小: {self._fmt_size(self.total_size)}")

    @staticmethod
    def _fmt_elapsed(seconds):
        if seconds < 1.0:
            return f"{seconds * 1000:.0f}ms"
        elif seconds < 60:
            return f"{seconds:.1f}s"
        elif seconds < 3600:
            return f"{int(seconds // 60)}m{int(seconds % 60)}s"
        else:
            return f"{int(seconds // 3600)}h{int((seconds % 3600) // 60)}m{int(seconds % 60)}s"

    @staticmethod
    def _fmt_size(n):
        if n >= 1024 * 1024 * 1024:
            return f"{n / (1024**3):.2f} GB"
        elif n >= 1024 * 1024:
            return f"{n / (1024**2):.2f} MB"
        elif n >= 1024:
            return f"{n / 1024:.2f} KB"
        else:
            return f"{n} B"

    @staticmethod
    def _filetime_to_str(ft):
        """FILETIME (100ns 自 1601-01-01 UTC) -> 本地时间字符串"""
        if ft == 0:
            return ""
        # Windows FILETIME epoch (1601) -> Unix epoch (1970) 差值，单位 100ns
        EPOCH_DIFF = 116444736000000000
        try:
            ts = (ft - EPOCH_DIFF) / 10_000_000
            from datetime import datetime
            return datetime.fromtimestamp(ts).strftime("%Y-%m-%d %H:%M")
        except (OSError, OverflowError, ValueError):
            return ""


if __name__ == "__main__":
    ScannerApp().mainloop()
