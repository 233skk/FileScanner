# FileScanner

多线程文件系统扫描器，C++ DLL + Python GUI。

## 构建

```bash
# 需要 MinGW (g++) 和 CMake
./build.bat
```

## 使用

```bash
# GUI
./startGUI.bat

# 命令行
build/FileScannerTest.exe [文件夹路径]
```

## 结构

```
src/     C++ 核心（DLL）
gui/     Python Tkinter 界面
test/    命令行测试程序
```
