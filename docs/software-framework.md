# 软件框架说明

本文描述 Code-M8 主控骨架的分层、依赖与单拍语义。

## 分层

- apps/wash_controller：进程入口
- core/：bootstrap、runtime、scheduler、tick
- ports/：hal、storage、application（命令/调度同步端口）
- adapters/：os/linux、hal/sim_hw、ui/cli、outbound、config、background
- application/、domain/
- assets/configs/

## 主链路

main → app_bootstrap → scheduler → control_tick → wash_control → domain。

## 规则

domain 不依赖 adapters 实现；测试不 include src/；命令经 line_command。

## 扩展

新 HAL：adapters/hal；新调度：adapters/os；新存储：ports/storage。
