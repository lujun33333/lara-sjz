# 三角洲独立悬浮工程

当前参照：本地官方 `三角洲行动_1.201.37117（正版）.ipa`，版本 1.201.37117（68），进程 `DeltaForceClient`，游戏包名 `com.tencent.tmgp.dfm`。App 自身标识为 `com.local.sjz`，产品名 `SJZ Overlay`。

启动页 → laramgr 串行会话 → sjzmem 读取 → SJZCollector → sjzesp_item_t → SJZHUDBridge 前台 Metal / 后台 CoreAnimation。原平台初始化和悬浮窗托管保留；王者专用采集、技能、自动输入、头像下载、版本 profile 与卡密界面已移除。

源码接入：人物、物资和盒子的外部采集与绘制。普通自瞄开关走独立虚拟 HID 触摸路线：在主程序版本与数据链有效时，选择屏幕内真人的 actor/root 中心作为躯干近似点；HID ready 和发送确认后，用横、纵探测触摸测量相机角变化，再按屏幕误差逐步移动。探测或反馈失败、目标或场景变化时释放触摸。该路线不要求可写传输，也不依赖 IPA 内的新增模块。官方包的骨骼索引、头/腿目标、输入板及真实视线尚无独立验证；头/腿或真实视线选项开启时明确停用。建议首轮设备验收先选“始终”触发并保持自瞄关闭，确认采集数据后再启用。

运行时按官方主程序 UUID `e64c789b-63ad-34c9-b22e-c2bfc3693e52` 定位目标。官方包主程序 `cryptid=1`，与旧改包的主程序 VM 布局相同；这支持版本定位，但不替代真机数据读取验收。出货路径不写旧版 `controller+0x3d8` 或任何新增模块配置。保留完整读取、数组边界和场景切换检查。

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

通用构建入口统一转到此脚本。公开仓库的 CI 构建悬浮 App，并在最终工作流中运行 `--package` 上传 ad-hoc 签名的悬浮 App IPA、未签名 App 和构建日志；官方三角洲 IPA 保持原样。安装与 HID 输入效果仍需对应设备环境验证。

源码内的样本定位信息只适用于 README 首段所列版本；设备效果需要另行验证。来源与第三方许可见 LICENSE 和 lara/licenses。
