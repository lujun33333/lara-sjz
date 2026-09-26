# 三角洲独立悬浮工程

当前参照：本地 `三角洲行动-cn..ipa`，三角洲行动 1.201.37117（68），进程 `DeltaForceClient`，游戏包名 `com.tencent.tmgp.dfm`。App 自身标识为 `com.local.sjz`，产品名 `SJZ Overlay`。

启动页 → laramgr 串行会话 → sjzmem 读取 → SJZCollector → sjzesp_item_t → SJZHUDBridge 前台 Metal / 后台 CoreAnimation。原平台初始化和悬浮窗托管保留；王者专用采集、技能、自动输入、头像下载、版本 profile 与卡密界面已移除。

源码接入：真人骨骼、人物方框/射线、名称、血量、距离、护甲、武器、AI 基础信息、物资和统一盒子显示。自瞄保留目标筛选、三轴速度预判、速度/部位/触发配置的模拟计算测试；当前版游戏视角写入目标尚未证实，生产路径停止写入并显示原因。真实视线查询尚无可靠外部提供者。AI 骨骼、盒子归属分类及武器控制未接入。

运行时按主程序 UUID `e64c789b-63ad-34c9-b22e-c2bfc3693e52` 定位目标；自瞄路径另核对新增模块 UUID `3c4bb519-bd5d-3ce2-a44f-7febe809a8ab`。两个 UUID 均匹配仍不足以证明旧版 `controller+0x3d8` 是本版的有效视角输出。保留完整读取、数组边界和场景切换检查。布局证据仅覆盖上述参照包，不代表任意新版本都兼容。

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

通用构建入口统一转到此脚本。公开仓库的 `main` 推送、PR 和手动触发会运行 macOS 无签名编译，并上传 App 与构建日志；不会自动打包或签名。

源码内的样本定位信息只适用于 README 首段所列版本；设备效果需要另行验证。来源与第三方许可见 LICENSE 和 lara/licenses。
