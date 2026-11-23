# W1个人周报｜控制与实验主责 Rato77
> 第七组｜赛道B 永磁吸附爬壁机器人

## ①证据（链接）
- 个人仓库主页：https://github.com/Rato77/robot-training-portfolio
- G0安全审查文档：[W1_G0_review.md](../W1_G0_review.md)
- LED测试代码：[led_test/](../../led_test/)
- 串口测试代码：[uart_test/](../../uart_test/)
- 电机测试代码（上电默认停机）：[motor_test/](../../motor_test/)
- 编码器读取Demo脚本：[encoder_test/](../../encoder_test/)

## ②本周我证明了什么
1. 完成Git/GitHub完整工作流，可以新建、提交、推送代码文档；
2. 完成ESP32点灯、串口通信、电机基础测试，motor_test内置上电默认停机安全逻辑；
3. 对照实训手册附件E1、E2完成G0安全准入与强磁授权12项条目审查，输出降级方案、实操痛点、修改建议；
4. 完成首个AB相编码器读取Demo脚本，可以通过外部中断获取编码器脉冲计数并串口输出。

## ③遇到的问题与已验证原因
1. GitHub页面底部Commit提交框浏览器窗口过小被遮挡，通过缩小浏览器页面解决；
2. Markdown相对链接路径容易写错，写完后切换Preview预览校验跳转有效性。

## ④下周要验证的最关键假设
> W2关键假设：JGA25‑370电机搭配双路驱动模块，PWM输出可以实现电机正反转控制，编码器可以稳定读取脉冲。
