# 三角洲独立悬浮工程

当前参照：本地官方 `三角洲行动_1.201.37117（正版）.ipa`，版本 1.201.37117（68），进程 `DeltaForceClient`，游戏包名 `com.tencent.tmgp.dfm`。独立悬浮 App 的包名为 `com.lujun33333.sjz.overlay`，桌面名称为“三角洲悬浮”；它与王者 AX 的本地工程包名 `com.ax.ax` 不同。

启动页 → laramgr 串行会话 → sjzmem 读取 → SJZCollector → sjzesp_item_t → SJZHUDBridge 前台 Metal / 后台 CoreAnimation。原平台初始化和悬浮窗托管保留；王者专用采集、技能、自动输入、头像下载、版本 profile 与卡密界面已移除。

源码接入：人物、物资和死亡盒的外部采集与绘制。菜单保留中文人物、物资、自瞄项。cn 允许 AI 后同样使用固定 18 点算法；已修正骨数组主入口 `mesh+0xa18` 与备用 `mesh+0x730`，完整、稳定、几何条件通过后才发布。头部／骨骼颜色按原版默认 `mesh+0x898` 位读取规则：真人真绿假红、AI 白；该原始字段不是引擎 LOS，也不是本版反射的 `bRecentlyRendered(+0x8b8)`。

Bot显示标志遵循原模块 `PlayerState+0x39e` 整字节零判断；本版UE真实 `bIsABot` 位为 `0x08`，两者不等价。确认没有PlayerState与读取失败有独立来源标记，未知分类不发布。

武器名称按cn完整66项原表及数字fallback显示，名称读取ID为4字节；自瞄弹速独立读取同偏移8字节，保留完整64位消费。普通预测直接计算相机相对坐标上的XY预判角，Z不预判；自瞄页开启总开关后才显示速度、范围、触发方式和部位。

自瞄构造18个真实骨点候选，头／胸／腿分别优先 mesh31／5／59，再按屏幕中心距离排序，范围边界严格小于。控制角度来自 Controller，目标角来自相机位置与所选点，按 cn 角差规范化及速度插值。输出已改为受版本、写能力、会话、场景和目标身份约束的 Controller `+0x3d8/+0x3dc` 写入与读回，保留 roll；无写能力时明确停用，不回退 HID。失败在同会话锁存，关闭自瞄或重连后解除。旧 HID 文件和旧弹速副本已移除。本轮只在模拟内存中执行写入回归，未向实际游戏进程写入；设备消费效果仍待验。原版真实 LOS 的引擎调用尚未接通。依据及剩余差异见 [逐项对照](docs/SJZ_DRAW_ALIGNMENT.md)。

运行时按官方主程序 UUID `e64c789b-63ad-34c9-b22e-c2bfc3693e52` 定位目标。官方包主程序 `cryptid=1`，与 cn 包对应主程序布局相同；cn 主模块反射 `ControlRotation` 和原生 getter/setter 的 `+0x3d8` 消费链已静态核实，这支持本版本字段实现，不替代手机运行验收。默认自瞄关闭，速度／范围无保存值时为零；有效写能力是输出前置条件。

## 检查

```sh
bash tests/run_sjz_tests.sh
python tests/sjz_source_consistency_test.py
```

## 构建

需要 macOS/Xcode。默认只构建 App，显式 `--package` 才打包：

```sh
bash scripts/build_sjz.sh
bash scripts/build_sjz.sh --debug
bash scripts/build_sjz.sh --package
```

通用构建入口统一转到此脚本。公开仓库的 CI 构建悬浮 App，并在最终工作流中运行 `--package` 上传 ad-hoc 签名的悬浮 App IPA、未签名 App 和构建日志；官方三角洲 IPA 保持原样。安装、权限与控制角度的实际消费效果仍需对应设备环境验证。

源码内的样本定位信息只适用于 README 首段所列版本；设备效果需要另行验证。来源与第三方许可见 LICENSE 和 lara/licenses。
