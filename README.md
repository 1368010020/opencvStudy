# opencvStudy

[![Build and Release](https://github.com/1368010020/opencvStudy/actions/workflows/build.yml/badge.svg)](https://github.com/1368010020/opencvStudy/actions/workflows/build.yml)

一个 Qt6 + OpenCV4 写的 OpenCV 学习/练习工具，Fluent 风格界面。左侧按分组导航挑算子，中间原图/处理结果实时对比，右侧滑块调参数、看对应的真实 C++ 代码、看处理耗时。为了面试复习 OpenCV 知识点而做，覆盖点运算、滤波、形态学、颜色空间、几何变换、直方图、边缘与梯度、霍夫变换、阈值进阶、特征与匹配、轮廓、视频分析、目标检测等 30+ 个算子，附带术语手册和点击像素看邻域/卷积计算过程的交互式学习功能。

## 下载直接用

不想自己编译的话，去 [Releases 页面](https://github.com/1368010020/opencvStudy/releases) 下载最新的 `OpenCVPractice-windows-x64.zip`，解压后双击 `OpenCVPractice.exe` 就能跑（已经把 Qt 和 OpenCV 的运行时 DLL 都打包进去了）。仅支持 Windows x64。

也可以在 [Actions 页面](https://github.com/1368010020/opencvStudy/actions/workflows/build.yml) 点最新一次成功的构建，下载 Artifacts 里同名的 zip——这是每次推送代码后自动构建出来的最新版本，不需要等打 tag 发正式 Release。

## 自己编译

依赖：Qt 6.8+、vcpkg（清单模式，`vcpkg.json` 里声明了 opencv4 及所需 feature）、CMake 3.21+、MSVC 2022。

```powershell
# 设置好 VCPKG_ROOT 环境变量指向你的 vcpkg 安装目录后
cmake --preset release
cmake --build build/release --config Release
```

首次配置会由 vcpkg 自动编译安装 OpenCV，耗时较长；之后重复构建会复用已安装的包。

## 技术栈

C++17 / Qt 6.8 Widgets / OpenCV 4 / CMake + vcpkg / GitHub Actions（自动构建+发版）
