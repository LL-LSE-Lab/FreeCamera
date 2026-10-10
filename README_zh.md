# FreeCamera  

[![Minecraft - Version](https://img.shields.io/badge/minecraft-v1.26.51.x_(Bedrock)-black)](https://feedback.minecraft.net/hc/en-us/sections/360001186971-Release-Changelogs)
![GitHub Tag](https://img.shields.io/github/v/tag/YUMU1658/FreeCamera)
![GitHub License](https://img.shields.io/github/license/GroupMountain/FreeCamera)

[ [简体中文](README_zh.md) | [English](README.md) ]

## 🎇 特点

- **无假人占用** - 纯数据包解决方案，保障服务器性能。

- **安全机制** - 受到伤害时自动切换回生存模式。

- **纯客户端** - 通过控制客户端状态实现，无需侵入服务端修改。

- **指令简洁** - 使用 `/freecamera` 或 `/fc` 快速切换模式。

## 📦 安装

1. 📋 安装依赖：
    - 确保已安装 [`LeviLamina`](https://github.com/LiteLDev/LeviLamina)。
    - 下载并安装  [`GMLIB`](https://github.com/GroupMountain/GMLIB-Release)。
2. 🎯 安装 FreeCamera
    - 从[发行版](https://github.com/YUMU1658/FreeCamera/releases)下载最新版本。
    - 将解压后的 `FreeCamera` 文件夹放入服务器 `./plugins/` 目录。

## 🎍 使用

- **进入自由视角**：在游戏中执行 `/freecamera` 或 `/fc`。

- **退出自由视角**：重复执行上述指令，或受到伤害时自动退出。

## 🧪 原理

本插件通过伪造数据包使客户端进入旁观者模式，而服务器端仍为玩家实际游戏模式。  
由于服务端侧逻辑不变，玩家仍会正常受到环境伤害。

## 📝 反馈与贡献
- 发现漏洞？请提交 [问题报告](https://github.com/GroupMountain/FreeCamera/issues)。  
- 欢迎贡献代码！请 Fork 项目并提交拉取请求。

## 📜 开源

基于 GPL-V3.0 协议分发。
详见 [LICENSE](LICENSE) 文件。