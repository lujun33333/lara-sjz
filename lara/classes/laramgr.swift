//
//  laramgr.swift
//  lara
//
//  Created by ruter on 23.03.26.
//

import AVFoundation
import Combine
import Foundation
import Darwin
import notify
import UIKit
import WebKit

private let sjzHUDActionHandler: @convention(c) (Int32) -> Void = { action in
    DispatchQueue.main.async {
        switch action {
        case 1:
            if laramgr.shared.sjzAttached {
                laramgr.shared.sjzDetach()
            } else {
                laramgr.shared.prepareSJZEnvironment()
            }
        case 2:
            laramgr.shared.launchSJZGame()
        case 3:
            laramgr.shared.setGameHUD(false)
        default:
            break
        }
    }
}

private func loadMutablePropertyListDictionary(from url: URL) throws -> NSMutableDictionary {
    let data = try Data(contentsOf: url)
    var format = PropertyListSerialization.PropertyListFormat.binary
    let plist = try PropertyListSerialization.propertyList(
        from: data,
        options: [.mutableContainersAndLeaves],
        format: &format
    )
    guard let dict = plist as? NSMutableDictionary else {
        throw "Property list root is not a dictionary."
    }
    return dict
}

private func clearImmutableForOverwriteIfNeeded(path: String) -> String? {
    let majorVersion = ProcessInfo.processInfo.operatingSystemVersion.majorVersion
    guard majorVersion == 16 else { return nil }

    let fm = FileManager.default
    guard let attributes = try? fm.attributesOfItem(atPath: path) else { return nil }

    var updates: [FileAttributeKey: Any] = [:]
    if (attributes[.immutable] as? NSNumber)?.boolValue == true {
        updates[.immutable] = false
    }
    if (attributes[.appendOnly] as? NSNumber)?.boolValue == true {
        updates[.appendOnly] = false
    }
    guard !updates.isEmpty else { return nil }

    do {
        try fm.setAttributes(updates, ofItemAtPath: path)
        return nil
    } catch {
        return "清除不可变属性失败：\(error.localizedDescription)"
    }
}

final class laramgr: ObservableObject {
    @Published var log: String = ""
    @Published var hasOffsets: Bool = false
    @Published var dsrunning: Bool = false
    @Published var dsready: Bool = false
    @Published var dsattempted: Bool = false
    @Published var dsfailed: Bool = false
    @Published var dsprogress: Double = 0.0
    @Published var kernbase: UInt64 = 0
    @Published var kernslide: UInt64 = 0
    
    @Published var kaccessready: Bool = false
    @Published var kaccesserror: String?
    @Published var fileopinprogress: Bool = false
    @Published var testresult: String?
    #if !DISABLE_REMOTECALL
    @Published var rcrunning: Bool = false {
        didSet { if !rcrunning { resumeSJZHostingRequests() } }
    }
    @Published var eligibilitystate: Bool?
    @Published var eu1progress: Double = 0.0
    @Published var eu1running: Bool = false
    @Published var eu2progress: Double = 0.0
    @Published var eu2running: Bool = false
    @Published var rcLastError: String?
    #endif
    
    @Published var vfsready: Bool = false
    @Published var vfsinitlog: String = ""
    @Published var vfsattempted: Bool = false
    @Published var vfsfailed: Bool = false
    @Published var vfsrunning: Bool = false
    @Published var vfsprogress: Double = 0.0
    @Published var sbxready: Bool = false
    @Published var sbxattempted: Bool = false
    @Published var sbxfailed: Bool = false
    @Published var sbxrunning: Bool = false
    @Published var rcready: Bool = false
    @Published var rcfailed: Bool = false
    @Published var showrespring: Bool = false
    
    @Published var showLogs: Bool = false
    
    var sbProc: RemoteCall?
    private var sjzSpringBoardInstallRunning = false {
        didSet { if !sjzSpringBoardInstallRunning { resumeSJZHostingRequests() } }
    }
    private var sjzHostingRequests: [() -> Void] = []
    private var sjzTerminating = false
    private var sjzSceneDisconnecting = false
    private var sjzSceneEpoch: UInt64 = 0
    private var sjzLaunchPending = false
    lazy var ytProc = RemoteCall(process: "youtube", useMigFilterBypass: false)
    @Published var sjzAttached: Bool = false
    @Published var sjzRunning: Bool = false
    @Published var sjzBase: UInt64 = 0
    @Published var sjzTransportName: String = "none"
    @Published var sjzTransportCapabilities: UInt64 = 0
    @Published var sjzCanWrite: Bool = false
    @Published var sjzStatus: String = "未运行"
    @Published var sjzGameHUDEnabled: Bool = false
    @Published var sjzGameHUDActive: Bool = false
    @Published var sjzGameHUDStatus: String = "未启动"
    @Published var sjzMeasuredFPS: Double = 0
    @Published var sjzChainDiagnostic: String = "等待采集"
    private var sjzGameHUDSessionArmed: Bool = false
    private var sjzFPSWindowStart = Date()
    private var sjzFPSFrameCount: Int = 0
    private var audioPlayer: AVAudioPlayer?
    private var audioBackgroundTask: UIBackgroundTaskIdentifier = .invalid
    private var audioObservers: [NSObjectProtocol] = []
    private var audioRecoveryEpoch: UInt64 = 0
    private var audioKeepAliveEnabled = false
    private var audioWatchdog: DispatchSourceTimer?
    
    static let shared = laramgr()
    static let fontpath = "/System/Library/Fonts/Core/SFUI.ttf"
    static let italicfontpath = "/System/Library/Fonts/Core/SFUIItalic.ttf"
    static let monofontpath = "/System/Library/Fonts/Core/SFUIMono.ttf"
    init() {
        sjzWorker.setSpecific(key: sjzWorkerKey, value: 1)
        sjzhud_set_action_callback(sjzHUDActionHandler)
    }

    struct AppInfo {
        let executable: String
        let displayName: String
        let bundleName: String
        let dataFolder: String
        let bundleFolder: String
    }
    
    func run(completion: ((Bool) -> Void)? = nil) {
        guard !dsrunning else { return }
        if dsready || ds_is_ready() {
            dsready = true
            dsfailed = false
            dsprogress = 1.0
            kernbase = ds_get_kernel_base()
            kernslide = ds_get_kernel_slide()
            logmsg("(ds) 内核读写已就绪，跳过重复注入")
            completion?(true)
            return
        }
        dsrunning = true
        dsready = false
        dsfailed = false
        dsattempted = true
        dsprogress = 0.0
        log = ""
        
        ds_set_log_callback { messageCStr in
            guard let messageCStr else { return }
            let message = String(cString: messageCStr)
            DispatchQueue.main.async {
                laramgr.shared.logmsg("(ds) \(message)")
            }
        }
        ds_set_progress_callback { progress in
            DispatchQueue.main.async {
                laramgr.shared.dsprogress = progress
            }
        }
        
        DispatchQueue.global(qos: .userInitiated).async { [weak self] in
            let result = ds_run()
            
            DispatchQueue.main.async {
                guard let self else { return }
                self.dsrunning = false
                let success = result == 0 && ds_is_ready()
                if success {
                    self.dsready = true
                    self.dsfailed = false
                    self.kernbase = ds_get_kernel_base()
                    self.kernslide = ds_get_kernel_slide()
                    self.logmsg("\n(ds) 漏洞利用成功！")
                    self.logmsg(String(format: "(ds) 内核基址：0x%llx", self.kernbase))
                    self.logmsg(String(format: "(ds) 内核偏移：0x%llx\n", self.kernslide))
                    globallogger.log("(ds) 漏洞利用成功！")
                    globallogger.log(String(format: "(ds) 内核基址：0x%llx", self.kernbase))
                    globallogger.log(String(format: "(ds) 内核偏移：0x%llx", self.kernslide))
                    globallogger.divider()
                } else {
                    self.dsfailed = true
                    self.logmsg("\n漏洞利用失败。\n")
                    globallogger.log("漏洞利用失败。")
                    globallogger.divider()
                }
                self.dsprogress = 1.0
                completion?(success)
            }
        }
    }
    
    func logmsg(_ message: String) {
        DispatchQueue.main.async {
            self.log += message + "\n"
            // 防止日志无限增长：超长时只保留尾部，避免大字符串 append 拖垮主线程触发看门狗
            if self.log.count > 200_000 {
                self.log = String(self.log.suffix(100_000))
            }
            globallogger.log(message)
        }
    }
    
    func kread64(address: UInt64) -> UInt64 {
        guard dsready else { return 0 }
        return ds_kread64(address)
    }
    
    func kwrite64(address: UInt64, value: UInt64) {
        guard dsready else { return }
        ds_kwrite64(address, value)
    }
    
    func kread32(address: UInt64) -> UInt32 {
        guard dsready else { return 0 }
        return ds_kread32(address)
    }
    
    func kwrite32(address: UInt64, value: UInt32) {
        guard dsready else { return }
        ds_kwrite32(address, value)
    }
    
    func panic() {
        guard dsready else { return }
        
        globallogger.log("触发内核崩溃")
        DispatchQueue.main.asyncAfter(deadline: .now() + 5) {
            let kernbase = ds_get_kernel_base()
            globallogger.log("正在向内核基址的只读内存写入")
            ds_kwrite64(kernbase, 0xDEADBEEF)
        }
    }
    
    func respring() {
        showrespring = true
    }
    
    // MARK: - 三角洲进程内存读取
    
    // All SJZ operations run on this queue. The main queue owns UI settings.
    private let sjzWorker = DispatchQueue(label: "lara.sjz.session", qos: .userInitiated)
    private let sjzWorkerKey = DispatchSpecificKey<UInt8>()
    // AX destroy wrapper@0x1006ffba8 retains failed objects in a deduplicated
    // pending registry.  This dictionary is accessed only on sjzWorker.
    private var sjzPendingRemoteCleanup: [ObjectIdentifier: RemoteCall] = [:]
    private var sjzTimer: DispatchSourceTimer?
    private var sjzEpoch: UInt64 = 0
    private var sjzLaunchEpoch: UInt64 = 0
    private var sjzLastResult = ""
    private var sjzLastResultTime = Date.distantPast
    private var sjzTickNumber: UInt64 = 0
    private var sjzLastDiagnosticStatus: Int32 = -1
    private var sjzLastDiagnosticPublishedCount = 0
    private var sjzLastHUDText = ""
    private var sjzLastHUDUpdateTime = Date.distantPast
    private var sjzLastHUDControlFlags = UInt32.max
    private var sjzLastConfigFingerprint: UInt64 = UInt64.max
    private func sjzImageReadable(_ base: UInt64) -> Bool {
        var words=[UInt32](repeating:0,count:8)
        let count=words.withUnsafeMutableBytes { sjz_read(base,$0.baseAddress,$0.count) }
        return count==32 && words[0]==0xfeedfacf && words[1]==0x0100000c && words[3]==2
    }

    func initializeSJZEnvironment() {
        prepareSJZEnvironment(connectWhenReady: false)
    }


    func setSJZControlPanelPresented(_ presented: Bool) {
        // The hosted menu is the single control surface in both apps. Do not
        // mount a second SwiftUI copy or rotate the Lara scene underneath it.
        if presented {
                if !sjzGameHUDEnabled { setGameHUD(true) }
            sjzhud_set_panel_visible(true)
        } else {
            sjzhud_set_panel_visible(false)
        }
    }
    func openSJZControlPanel() {
        if dsready && hasOffsets {
            setSJZControlPanelPresented(true)
        } else {
            initializeSJZEnvironment()
        }
    }
    func prepareSJZEnvironment(connectWhenReady: Bool = true) {
        guard !sjzTerminating, !sjzSceneDisconnecting,
              !dsrunning, !sjzRunning, !sjzAttached else { return }
        let sceneEpoch = sjzSceneEpoch
        if !dsready {
            offsets_init()
            sjzStatus = "正在初始化内核环境"
            run { [weak self] success in
                guard let self else { return }
                guard self.sjzSceneEpoch == sceneEpoch,
                      !self.sjzTerminating,
                      !self.sjzSceneDisconnecting else { return }
                if success {
                    self.prepareSJZEnvironment(connectWhenReady: connectWhenReady)
                } else {
                    self.sjzLaunchPending = false
                    self.sjzStatus = "内核环境初始化失败"
                }
            }
            return
        }
        if !hasOffsets {
            sjzRunning = true
            sjzStatus = "正在获取并解析内核偏移"
            logmsg("(sjz) 正在获取 kernelcache 并解析内核偏移")
            DispatchQueue.global(qos: .userInitiated).async { [weak self] in
                let fetched = fetchkcache()
                let loaded = fetched && dlkcache()
                DispatchQueue.main.async {
                    guard let self else { return }
                    guard self.sjzSceneEpoch == sceneEpoch,
                          !self.sjzTerminating,
                          !self.sjzSceneDisconnecting else { return }
                    self.hasOffsets = loaded
                    self.sjzRunning = false
                    if loaded {
                        if self.sjzLaunchPending {
                            self.launchSJZGame()
                        } else if connectWhenReady {
                            self.sjzStatus = "内核偏移已就绪，正在连接三角洲行动"
                            self.logmsg("(sjz) 内核偏移已就绪，继续连接 DeltaForceClient")
                            self.sjzAttach()
                        } else {
                            self.sjzStatus = "内核环境已就绪，可以启动游戏"
                            self.logmsg("(sjz) 内核环境和偏移已就绪")
                            self.setSJZControlPanelPresented(true)
                        }
                    } else {
                        self.sjzLaunchPending = false
                        self.sjzStatus = "内核偏移获取失败"
                        self.logmsg("(sjz) kernelcache 获取或偏移解析失败")
                    }
                }
            }
            return
        }
        if sjzLaunchPending {
            launchSJZGame()
        } else if connectWhenReady {
            sjzAttach()
        } else {
            sjzStatus = "内核环境已就绪，可以启动游戏"
            setSJZControlPanelPresented(true)
        }
    }
    func launchSJZGame() {
        guard !sjzTerminating, !sjzSceneDisconnecting else { return }
        let support = axDeviceSupportStatus()
        guard support.isSupported else {
            sjzLaunchPending = false
            sjzStatus = "当前环境不支持：\(support.reason ?? support.identifier)"
            return
        }
        guard dsready, hasOffsets else {
            sjzLaunchPending = true
            sjzStatus = "正在初始化环境，完成后自动启动游戏"
            initializeSJZEnvironment()
            return
        }
        sjzLaunchPending = false
        // The hosted menu is already the controller. Keeping the SwiftUI copy
        // mounted here caused the duplicated panels in device captures.
        setGameHUD(true)
        sjzLaunchEpoch &+= 1
        let launchEpoch = sjzLaunchEpoch
        sjzGameHUDActive = sjzhud_is_enabled()
        let reason = String(cString: sjzhud_last_error())
        sjzGameHUDStatus = sjzGameHUDActive
            ? "本地双窗口已就绪"
            : (reason.isEmpty ? "悬浮窗未就绪，等待跨 App 托管" : reason)
        sjzStatus = "正在启动三角洲行动"
        logmsg("(sjz.hud) preparing AX host launch ready=\(sjzGameHUDActive ? "yes" : "no") error=\(reason.isEmpty ? "none" : reason)")
        prepareSJZSpringBoardHosting { [weak self] success in
            guard let self, launchEpoch == self.sjzLaunchEpoch else { return }
            guard success else {
                self.sjzStatus = "跨 App 托管失败，游戏未启动"
                return
            }
            // 游戏首次进入时直接展示 AX 控制面板；用户可用“×”收起为悬浮球，
            // “退出 HUD”仍只负责完整注销双窗口。
            sjzhud_set_panel_visible(true)
            self.openSJZGame(epoch: launchEpoch)
        }
    }

    // AX orders mode 0 cleanup, mode 1 installation, then game launch.
    // AX failure callbacks restore controls/report the error; only success
    // reaches the LSApplicationWorkspace launch callback.
    private func resumeSJZHostingRequests() {
        guard !rcrunning, !sjzSpringBoardInstallRunning, !sjzHostingRequests.isEmpty else { return }
        let requests = sjzHostingRequests
        sjzHostingRequests.removeAll()
        DispatchQueue.main.async { requests.forEach { $0() } }
    }

    // 两级托管。
    //
    // 上一轮我把第二级拆了，理由是「AX 的二进制里没有 SBS 托管类的字符串」。
    // 反汇编之后证明那是错的：AX 把字符串加密存在 __data 里，它确实用
    // objc_getClass(<加密串>) + objc_alloc_init 取托管控制器，再赋给自己的
    // _windowHostingController / _drawWindowHostingController。在加密二进制上
    // 「搜不到」不等于「没有」—— 这是这几轮反复拆坏的根因。
    //
    // 所以恢复两级：本地注册成功就用本地；本地注册不到（iOS 26 上
    // SBSAccessibilityWindowHostingController 可能已不存在）就回落 SpringBoard。
    private func prepareSJZSpringBoardHosting(completion: @escaping (Bool) -> Void) {
        guard !sjzTerminating, !sjzSceneDisconnecting else {
            completion(false)
            return
        }
        guard dsready, sjzGameHUDSessionArmed else { completion(false); return }

        if sjzhud_local_hosting_ready() {
            sjzGameHUDStatus = "本地双窗口运行中"
            logmsg("(sjz.hud) local hosting ready (AX 双系统窗口模式)")
            completion(true)
            return
        }

        // A second launch request must reuse a live remote pair.  The old
        // path tore the pair down and then trusted a stale cached "ready".
        if sjzhud_springboard_hosting_ready() {
            sjzGameHUDStatus = "跨 App 双窗口运行中"
            logmsg("(sjz.hud) verified SpringBoard host already running")
            completion(true)
            return
        }

        let localDetail = String(cString: sjzhud_last_error())
        logmsg("(sjz.hud) local hosting unavailable (\(localDetail.isEmpty ? "none" : localDetail))；回落 SpringBoard 托管")

        if rcrunning || sjzSpringBoardInstallRunning {
            sjzHostingRequests.append { [weak self] in
                guard let self else { return }
                self.prepareSJZSpringBoardHosting(completion: completion)
            }
            return
        }
        if rcready, let remoteProcess = sbProc {
            installSJZSpringBoardHosting(remoteProcess, completion: completion)
            return
        }
        sjzSpringBoardInstallRunning = true
        logmsg("(sjz.hud) initializing SpringBoard RemoteCall before host rebuild")
        rcinit(process: "SpringBoard", migbypass: false) { [weak self] success in
            guard let self else { return }
            guard success else {
                self.sjzSpringBoardInstallRunning = false
                let detail = self.rcLastError ?? "RemoteCall 初始化失败"
                self.sjzGameHUDStatus = "跨 App 托管失败：\(detail)"
                self.logmsg("(sjz.hud) SpringBoard RemoteCall unavailable: \(detail)")
                completion(false)
                return
            }
            guard self.sjzGameHUDSessionArmed, let remoteProcess = self.sbProc else {
                if let abandonedProcess = self.sbProc {
                    self.sbProc = nil
                    self.rcready = false
                    self.sjzWorker.async { [weak self] in
                        guard let self else { return }
                        _ = self.destroyRemoteCallOnWorker(abandonedProcess)
                        for pendingProcess in Array(self.sjzPendingRemoteCleanup.values) {
                            _ = self.destroyRemoteCallOnWorker(pendingProcess)
                        }
                        DispatchQueue.main.async {
                            self.sjzSpringBoardInstallRunning = false
                            completion(false)
                        }
                    }
                } else {
                    self.sjzSpringBoardInstallRunning = false
                    completion(false)
                }
                return
            }
            self.sjzSpringBoardInstallRunning = false
            self.installSJZSpringBoardHosting(remoteProcess, completion: completion)
        }
    }

    private func installSJZSpringBoardHosting(_ remoteProcess: RemoteCall, completion: @escaping (Bool) -> Void) {
        guard sjzGameHUDSessionArmed, !sjzSpringBoardInstallRunning else { completion(false); return }
        if sjzhud_springboard_hosting_ready() {
            sjzGameHUDStatus = "跨 App 双窗口运行中"
            completion(true)
            return
        }
        sjzSpringBoardInstallRunning = true
        sjzWorker.async { [weak self, remoteProcess] in
            let removed = sjzhud_unregister_springboard_hosts(remoteProcess)
            if !removed {
                self?.logmsg("(sjz.hud) mode 0 unhost reported failure; continuing AX mode 1 rebuild")
            }
            let registered = sjzhud_register_springboard_hosts(remoteProcess)
            var ready = registered && sjzhud_springboard_hosting_ready()
            let detail = String(cString: sjzhud_last_error())
            if ready {
                usleep(1_200_000)
                // Do not report success after a concurrent mode-0 teardown
                // or a source-context replacement invalidated the mirrors.
                ready = sjzhud_springboard_hosting_ready()
            }
            if !ready, let self {
                if registered {
                    _ = sjzhud_unregister_springboard_hosts(remoteProcess)
                }
                _ = self.destroyRemoteCallOnWorker(remoteProcess)
                for pendingProcess in Array(self.sjzPendingRemoteCleanup.values) {
                    _ = self.destroyRemoteCallOnWorker(pendingProcess)
                }
            }
            let verifiedReady = ready
            DispatchQueue.main.async {
                guard let self else { return }
                let live = verifiedReady && sjzhud_springboard_hosting_ready()
                if !live, self.sbProc === remoteProcess {
                    self.sbProc = nil
                    self.rcready = false
                    self.rcLastError = detail.isEmpty ? "跨 App 双窗口托管失败" : detail
                }
                self.sjzSpringBoardInstallRunning = false
                defer { completion(live && self.sjzGameHUDSessionArmed && !self.sjzTerminating) }
                guard self.sjzGameHUDSessionArmed else { return }
                self.sjzGameHUDStatus = live
                    ? "跨 App 双窗口运行中"
                    : (detail.isEmpty ? "跨 App 双窗口托管失败" : detail)
                self.logmsg("(sjz.hud) SpringBoard CALayerHost ready=\(live ? "yes" : "no") error=\(detail.isEmpty ? "none" : detail)")
            }
        }
    }

    private func openSJZGame(epoch: UInt64) {
        guard !sjzTerminating, epoch == sjzLaunchEpoch else { return }
        logmsg("(sjz.launch) opening com.tencent.tmgp.dfm epoch=\(epoch)")
        // AX dispatches its success block to the main queue once, then calls
        // LSApplicationWorkspace synchronously from that block.
        let opened = sjzhud_open_deltaforce_application()
        guard epoch == sjzLaunchEpoch else { return }
        logmsg("(sjz.launch) open callback opened=\(opened ? "yes" : "no") epoch=\(epoch)")
        if opened {
            sjzStatus = "游戏已启动，等待 DeltaForceClient 进程"
            scheduleSJZAttachAfterLaunch(attempt: 0)
        } else {
            sjzStatus = "未能启动三角洲行动"
            logmsg("(sjz) LSApplicationWorkspace 启动失败")
            sjzLaunchEpoch &+= 1
            hideGameHUD("游戏启动失败，已注销悬浮窗")
        }
    }
    private func scheduleSJZAttachAfterLaunch(attempt: Int) {
        guard !sjzAttached, attempt < 15 else { return }
        let delay = attempt == 0 ? 2.5 : 1.5
        DispatchQueue.main.asyncAfter(deadline: .now() + delay) { [weak self] in
            guard let self, !self.sjzAttached else { return }
            if !self.sjzRunning { self.sjzAttach() }
            self.scheduleSJZAttachAfterLaunch(attempt: attempt + 1)
        }
    }
    func sjzAttach(process: String = "DeltaForceClient") {
        guard !sjzRunning, !sjzAttached, !sjzTerminating, !sjzSceneDisconnecting else { return }
        sjzRunning=true
        sjzEpoch &+= 1
        let epoch=sjzEpoch
        sjzStatus="正在连接三角洲行动"
        sjzWorker.async { [weak self] in
            guard let self else { return }
            let connected=process.withCString { sjz_connect($0) }
            let base=connected ? sjzesp_supported_game_base() : 0
            let valid=connected && base != 0 && self.sjzImageReadable(base)
            let transport=valid ? String(cString:sjz_transport_name()) : "none"
            let capabilities=valid ? sjz_transport_capabilities() : 0
            let canWrite=valid && sjz_transport_can_write()
            if !valid { sjz_disconnect() }
            DispatchQueue.main.async {
                guard epoch==self.sjzEpoch, !self.sjzTerminating, !self.sjzSceneDisconnecting else { return }
                self.sjzRunning=false
                self.sjzAttached=valid
                self.sjzBase=valid ? base : 0
                self.sjzTransportName=transport
                self.sjzTransportCapabilities=capabilities
                self.sjzCanWrite=canWrite
                transport.withCString { sjzhud_set_transport_state(valid,self.sjzCanWrite,$0) }
                if valid {
                    self.sjzStatus="已连接三角洲行动，等待对局"
                    self.startSJZLoop()
                } else {
                    self.sjzStatus="连接失败：未找到三角洲进程或主程序不可读"
                }
                self.logmsg(self.sjzStatus)
            }
        }
    }

    func sjzDetach() {
        // Detach is serialized on sjzWorker.  Do not drop a user request just
        // because attach/read is still in flight; epoch invalidation makes
        // the stale completion harmless and this block releases the session
        // after the queued operation.
        guard sjzAttached || sjzRunning || sjzTimer != nil else { return }
        sjzRunning = true
        sjzEpoch &+= 1
        sjzTimer?.cancel()
        sjzTimer = nil
        sjzWorker.async { [weak self] in
            guard let self else { return }
            // Invalidate, drain both independent readers, then clear caches
            // and release the transport (AX 0x1008071d0/0x100807374 order).
            sjzesp_reset()
            self.sjzLastDiagnosticStatus = -1
            self.sjzLastDiagnosticPublishedCount = 0
            sjz_disconnect()
            "none".withCString {
                sjzhud_set_transport_state(false, false, $0)
            }
            sjzhud_update_sjz_snapshot(nil, 0)
            self.sjzLastResult = ""
            self.sjzLastHUDText = ""
            self.sjzLastHUDControlFlags = UInt32.max
            self.sjzLastConfigFingerprint = UInt64.max
            self.sjzFPSWindowStart = Date()
            self.sjzFPSFrameCount = 0
            DispatchQueue.main.async {
                self.sjzAttached = false
                self.sjzBase = 0
                self.sjzTransportName = "none"
                self.sjzTransportCapabilities = 0
                self.sjzCanWrite = false
                self.sjzMeasuredFPS = 0
                self.sjzChainDiagnostic = "已断开"
                self.sjzRunning = false
                self.sjzStatus = "已断开"
                self.hideGameHUD("已断开")
                self.logmsg("三角洲工作队列已结束，端口、映射与会话已释放")
            }
        }
    }


    func setGameHUD(_ enabled: Bool) {
        sjzGameHUDEnabled = enabled
        UserDefaults.standard.set(false, forKey: "sjzGameHUDEnabled")
        if enabled {
            sjzGameHUDSessionArmed = true
            sjzTransportName.withCString {
                sjzhud_set_transport_state(sjzAttached, sjzCanWrite, $0)
            }
            // AX controllers and both local windows must exist before the app
            // opens DeltaForceClient. sjzhud_is_enabled only becomes true after both
            // registrations complete.
            let requested = sjzhud_set_enabled(true)
            sjzGameHUDActive = requested && sjzhud_is_enabled()
            if sjzAttached {
                updateGameHUD("三角洲已连接\n等待功能开关")
            } else {
                sjzGameHUDStatus = sjzGameHUDActive
                    ? "悬浮窗已准备\n等待三角洲进程"
                    : "悬浮窗创建失败"
            }
        } else {
            // Invalidate in-flight direct-float callbacks before cleanup.
            sjzLaunchEpoch &+= 1
            hideGameHUD("已关闭")
        }
    }
    private func updateGameHUD(_ text: String) {
        guard sjzGameHUDEnabled, sjzGameHUDSessionArmed, sjzAttached else { return }
        text.withCString { sjzhud_update_text($0) }
        sjzGameHUDActive = sjzhud_is_enabled()
        let error = String(cString: sjzhud_last_error())
        sjzGameHUDStatus = sjzGameHUDActive ? "双窗口运行中" :
            (error.isEmpty ? "双窗口未就绪" : error)
    }
    private func hideGameHUD(_ status: String) {
        sjzLaunchPending = false
        sjzGameHUDEnabled = false
        sjzGameHUDSessionArmed = false
        sjzGameHUDActive = false
        sjzGameHUDStatus = status
        rcdestroy { [weak self] in
            self?.logmsg("(sjz.hud) remote mode 0 completed; local windows stopped")
        }
    }
    private func startSJZLoop() {
        guard sjzTimer == nil else { return }
        let timer = DispatchSource.makeTimerSource(queue: sjzWorker)
        timer.schedule(deadline: .now(), repeating: .milliseconds(16), leeway: .milliseconds(2))
        timer.setEventHandler { [weak self] in self?.sjzFrame() }
        sjzTimer = timer
        timer.resume()
    }
    private func sjzFrame() {
        // Only this serial worker touches the reader and collector. Main owns UI.
        let frameStarted = DispatchTime.now().uptimeNanoseconds
        var base: UInt64=0, epoch: UInt64=0
        var width=0.0, height=0.0
        var config=sjzesp_config_t()
        DispatchQueue.main.sync {
            guard self.sjzAttached, !self.sjzTerminating, !self.sjzSceneDisconnecting else { return }
            base=self.sjzBase; epoch=self.sjzEpoch
            sjzhud_get_canvas_size(&width,&height)
            sjzhud_copy_sjz_config(&config)
        }
        let mainReady = DispatchTime.now().uptimeNanoseconds
        guard base != 0, width.isFinite, height.isFinite, width>1, height>1,
              width<16384, height<16384 else { return }
        sjzTickNumber &+= 1
        if sjzTickNumber % 60 == 0 {
            let pid=sjz_connected_pid()
            if pid>0 && kill(pid,0) == -1 && errno == ESRCH {
                DispatchQueue.main.async {
                    guard epoch==self.sjzEpoch else { return }
                    self.sjzDetach()
                }
                return
            }
        }
        var items=[sjzesp_item_t](repeating:sjzesp_item_t(),count:Int(SJZ_MAX_ITEMS))
        let collectStarted = DispatchTime.now().uptimeNanoseconds
        let count=items.withUnsafeMutableBufferPointer {
            Int(sjzesp_tick(base,UInt32(width),UInt32(height),&config,$0.baseAddress,Int32($0.count)))
        }
        let collectFinished = DispatchTime.now().uptimeNanoseconds
        let stats=sjzesp_stats()
        let status=String(cString:sjzesp_last_error())
        let aimStatus=String(cString:sjzesp_last_aim_status())
        let aimEnabled=(config.flags & (1 << 11)) != 0
        let report=aimEnabled ? "\(status) · \(aimStatus)" : status
        let frameNumber=sjzTickNumber
        let sampleTime=Int64(Date().timeIntervalSince1970 * 1000)
        let mainSyncMs = Double(mainReady-frameStarted) / 1_000_000
        let collectMs = Double(collectFinished-collectStarted) / 1_000_000
        let statusChanged = stats.status != sjzLastDiagnosticStatus
        let positiveBurst = count > 0 && sjzLastDiagnosticPublishedCount == 0
        sjzLastDiagnosticStatus = stats.status
        sjzLastDiagnosticPublishedCount = count
        let traceFrame = frameNumber % 12 == 0 || statusChanged || positiveBurst ||
            mainSyncMs > 250 || collectMs > 250
        if traceFrame {
            let state="(sjz.frame) ms=\(sampleTime) tick=\(frameNumber) sample=\(stats.sampleGeneration) status=\(stats.status) stage=\(stats.stage) mask=\(stats.sampleMask) reads=\(stats.readFailures) calls=\(stats.readCalls) actors=\(stats.actorCount) players=\(stats.playerCount) loot=\(stats.lootCount) published=\(count) flags=\(config.flags) maxDist=\(config.maxDistance) minLootLv=\(config.lootLevel) mainSyncMs=\(mainSyncMs) collectMs=\(collectMs) viewport=\(stats.viewportWidth),\(stats.viewportHeight)"
            let actor=" rootSlot=\(stats.rootSlotValue) rootEdge=\(stats.rootFailureEdge) actorHeader=\(stats.actorHeaderStartData),\(stats.actorHeaderStartCount)->\(stats.actorHeaderEndData),\(stats.actorHeaderEndCount) scanned=\(stats.scannedActors) candidates=\(stats.candidatePlayers),\(stats.candidateLoot) classCache=\(stats.classCacheHits),\(stats.classCacheMisses) recheck=\(stats.actorRecheckReason) lootReject=\(stats.lootRejectPosition),\(stats.lootRejectProjection),\(stats.lootRejectDistance),\(stats.lootRejectContainer),\(stats.lootRejectData),\(stats.lootRejectLevelRead),\(stats.lootRejectLevelFilter) lootPriceReadFailures=\(stats.lootPriceReadFailures)"
            let camera=" roots=\(stats.worldIdentity),\(stats.levelIdentity),\(stats.pawnIdentity) camera=\(stats.cameraX),\(stats.cameraY),\(stats.cameraZ),\(stats.cameraPitch),\(stats.cameraYaw),\(stats.cameraRoll),\(stats.cameraFov) local=\(stats.localX),\(stats.localY),\(stats.localZ)"
            let target=" target=\(stats.firstTargetIdentity),\(stats.targetWorldX),\(stats.targetWorldY),\(stats.targetWorldZ),\(stats.targetScreenX),\(stats.targetScreenY),\(stats.targetDistance) aim=\(aimEnabled ? 1 : 0):\(aimStatus)"
            globallogger.log(state+actor+camera+target)
        }
        let publishQueuedAt = DispatchTime.now().uptimeNanoseconds
        DispatchQueue.main.async {
            guard epoch==self.sjzEpoch, self.sjzAttached else { return }
            let queuedMs = Double(DispatchTime.now().uptimeNanoseconds-publishQueuedAt) / 1_000_000
            items.withUnsafeBufferPointer {
                sjzhud_update_sjz_snapshot_with_tick(count>0 ? $0.baseAddress : nil,
                                                     Int32(count), frameNumber)
            }
            if traceFrame {
                let postCollectMs = Double(publishQueuedAt-collectFinished) / 1_000_000
                self.logmsg("(sjz.publish) tick=\(frameNumber) count=\(count) postCollectMs=\(postCollectMs) queuedMs=\(queuedMs)")
            }
            self.sjzStatus=report
            self.sjzChainDiagnostic="人物 \(stats.playerCount) · 物资 \(stats.lootCount) · 读取失败 \(stats.readFailures)" +
                (aimEnabled ? " · \(aimStatus)" : "")
            if status != self.sjzLastResult {
                self.sjzLastResult=status
                self.logmsg("(sjz.collect) \(status)")
            }
            self.updateGameHUD(report)
            if stats.status==Int32(SJZ_STATUS_TRANSPORT) { self.sjzDetach() }
        }
    }

    func startBackgroundAudio() {
        guard !sjzTerminating, !sjzSceneDisconnecting else { return }
        audioKeepAliveEnabled = true
        installAudioObservers()
        if audioWatchdog == nil {
            // AX 0x100004694: main-queue watchdog, 1s period, 100ms leeway.
            let timer = DispatchSource.makeTimerSource(queue: .main)
            timer.schedule(deadline: .now() + 1, repeating: .seconds(1), leeway: .milliseconds(100))
            timer.setEventHandler { [weak self] in
                guard let self, self.audioKeepAliveEnabled,
                      self.audioPlayer?.isPlaying != true else { return }
                self.recoverBackgroundAudio()
            }
            audioWatchdog = timer
            timer.resume()
        }
        if !playBackgroundAudio() { recoverBackgroundAudio() }
    }

    private func installAudioObservers() {
        guard audioObservers.isEmpty else { return }
        let center = NotificationCenter.default
        // AX 0x1000044ac/4500/454c: block observers, object=nil, main queue.
        audioObservers.append(center.addObserver(forName: AVAudioSession.interruptionNotification,
                                                object: nil, queue: .main) { [weak self] _ in
            // AX does not filter interruption type or shouldResume options.
            self?.recoverBackgroundAudio()
        })
        audioObservers.append(center.addObserver(forName: AVAudioSession.mediaServicesWereResetNotification,
                                                object: nil, queue: .main) { [weak self] _ in
            self?.audioPlayer?.stop()
            self?.audioPlayer = nil
            self?.recoverBackgroundAudio()
        })
        audioObservers.append(center.addObserver(forName: AVAudioSession.routeChangeNotification,
                                                object: nil, queue: .main) { [weak self] notification in
            let reason = (notification.userInfo?[AVAudioSessionRouteChangeReasonKey] as? NSNumber)?.intValue ?? 0
            // AX 0x10000a578/594/5d4 accepts precisely reasons 1, 2, 4.
            if reason == 1 || reason == 2 || reason == 4 { self?.recoverBackgroundAudio() }
        })
        for name in [UIApplication.didEnterBackgroundNotification, UIApplication.didBecomeActiveNotification] {
            audioObservers.append(center.addObserver(forName: name, object: nil, queue: .main) { [weak self] _ in
                self?.recoverBackgroundAudio()
            })
        }
        audioObservers.append(center.addObserver(forName: UIApplication.willEnterForegroundNotification,
                                                object: nil, queue: .main) { [weak self] _ in
            self?.endAudioBackgroundTask()
        })
    }

    private func recoverBackgroundAudio() {
        guard audioKeepAliveEnabled, !sjzTerminating else { return }
        beginAudioBackgroundTask()
        audioRecoveryEpoch &+= 1
        let epoch = audioRecoveryEpoch
        // AX 0x100c81228, callback 0x10000a668: stale generations exit;
        // the first successful playback invalidates all remaining attempts.
        for delay in [0.0, 0.2, 0.5, 1.0, 2.0, 4.0, 8.0, 16.0] {
            DispatchQueue.main.asyncAfter(deadline: .now() + delay) { [weak self] in
                guard let self, self.audioKeepAliveEnabled, !self.sjzTerminating,
                      self.audioRecoveryEpoch == epoch else { return }
                if self.playBackgroundAudio() {
                    self.audioRecoveryEpoch &+= 1
                    self.endAudioBackgroundTask()
                }
            }
        }
    }

    private func playBackgroundAudio() -> Bool {
        do {
            try AVAudioSession.sharedInstance().setCategory(.playback, mode: .default, options: [.mixWithOthers])
            try AVAudioSession.sharedInstance().setActive(true)
            if let player = audioPlayer {
                return player.isPlaying || player.play()
            }
            let cache = try FileManager.default.url(for: .cachesDirectory, in: .userDomainMask,
                                                   appropriateFor: nil, create: true)
            let url = cache.appendingPathComponent("ax-hud-keepalive.wav")
            // AX 0x100009a18/0x100009c4c: mono 16-bit, 44100 Hz,
            // one second alternating -8/+8 PCM samples.
            var wav = Data()
            func append<T: FixedWidthInteger>(_ value: T) {
                var little = value.littleEndian
                Swift.withUnsafeBytes(of: &little) { wav.append(contentsOf: $0) }
            }
            wav.append(contentsOf: "RIFF".utf8)
            append(UInt32(88236))
            wav.append(contentsOf: "WAVEfmt ".utf8)
            append(UInt32(16)); append(UInt16(1)); append(UInt16(1))
            append(UInt32(44100)); append(UInt32(88200))
            append(UInt16(2)); append(UInt16(16))
            wav.append(contentsOf: "data".utf8)
            append(UInt32(88200))
            for index in 0..<44100 {
                append(Int16(index.isMultiple(of: 2) ? -8 : 8))
            }
            try wav.write(to: url, options: .atomic)
            let player = try AVAudioPlayer(contentsOf: url)
            player.numberOfLoops = -1
            player.volume = 0.08
            guard player.prepareToPlay(), player.isPlaying || player.play() else {
                throw "后台保活 WAV 播放失败"
            }
            audioPlayer = player
            return true
        } catch {
            logmsg("⚠️ 后台常驻音频启动失败：\(error.localizedDescription)")
            return false
        }
    }
    private func beginAudioBackgroundTask() {
        if audioBackgroundTask == .invalid {
            audioBackgroundTask = UIApplication.shared.beginBackgroundTask(withName: "AXHUDKeepAlive") { [weak self] in
                guard let self else { return }
                self.endAudioBackgroundTask()
            }
        }
    }
    private func endAudioBackgroundTask() {
        guard audioBackgroundTask != .invalid else { return }
        UIApplication.shared.endBackgroundTask(audioBackgroundTask)
        audioBackgroundTask = .invalid
    }
    func stopBackgroundAudio() {
        audioKeepAliveEnabled = false
        audioRecoveryEpoch &+= 1
        audioObservers.forEach { NotificationCenter.default.removeObserver($0) }
        audioObservers.removeAll()
        audioWatchdog?.cancel()
        audioWatchdog = nil
        audioPlayer?.stop()
        audioPlayer = nil
        endAudioBackgroundTask()
        try? AVAudioSession.sharedInstance().setActive(false, options: .notifyOthersOnDeactivation)
    }

    private struct SJZRemoteCleanupResult {
        var hostsRemoved = true
        var currentDestroyed = true
        var pendingRemaining = 0
    }

    // AX cleanup helper@0x1006fb8c0 executes directly when it is already on
    // the cleanup queue and dispatch_syncs only for a different queue.
    private func performSJZCleanupSync<T>(_ body: () -> T) -> T {
        if DispatchQueue.getSpecific(key: sjzWorkerKey) == 1 {
            return body()
        }
        return sjzWorker.sync(execute: body)
    }

    @discardableResult
    private func destroyRemoteCallOnWorker(_ remoteProcess: RemoteCall) -> Bool {
        let key = ObjectIdentifier(remoteProcess)
        let destroyed = sjzhud_remote_cleanup_succeeded(remoteProcess.destroy())
        if destroyed {
            sjzPendingRemoteCleanup.removeValue(forKey: key)
        } else {
            // Assignment by ObjectIdentifier is the pending-set dedup gate.
            sjzPendingRemoteCleanup[key] = remoteProcess
        }
        return destroyed
    }

    private func cleanupRemoteCallsOnWorker(
        _ remoteProcess: RemoteCall?
    ) -> SJZRemoteCleanupResult {
        var result = SJZRemoteCleanupResult()
        // remote unhost helper@0x100700284 releases draw before menu and
        // clears its fields regardless of either result.
        result.hostsRemoved = sjzhud_unregister_springboard_hosts(remoteProcess)
        if let remoteProcess {
            result.currentDestroyed = destroyRemoteCallOnWorker(remoteProcess)
        }

        // Retry one snapshot after registering the current failure. A second
        // failure stays strongly retained for the next serialized cleanup.
        let pendingSnapshot = Array(sjzPendingRemoteCleanup.values)
        for pendingProcess in pendingSnapshot {
            _ = destroyRemoteCallOnWorker(pendingProcess)
        }
        if let remoteProcess {
            result.currentDestroyed =
                sjzPendingRemoteCleanup[ObjectIdentifier(remoteProcess)] == nil
        }
        result.pendingRemaining = sjzPendingRemoteCleanup.count
        return result
    }

    private func drainSJZRemoteTeardown() {
        let remoteProcess = sbProc
        rcrunning = true
        var result = SJZRemoteCleanupResult()
        if Thread.isMainThread {
            // sjzWorker frame reads may synchronously sample UIKit. Keep the
            // main run loop serviceable while a non-worker caller enters the
            // exact queue-specific/sync helper, then continue only after its
            // completion has returned to main.
            var finished = false
            DispatchQueue.global(qos: .userInitiated).async {
                let cleanupResult = self.performSJZCleanupSync {
                    self.cleanupRemoteCallsOnWorker(remoteProcess)
                }
                DispatchQueue.main.async {
                    result = cleanupResult
                    finished = true
                    CFRunLoopStop(CFRunLoopGetMain())
                }
            }
            while !finished {
                CFRunLoopRun()
            }
        } else {
            result = performSJZCleanupSync {
                cleanupRemoteCallsOnWorker(remoteProcess)
            }
        }
        rcrunning = false
        if sbProc === remoteProcess {
            rcready = false
            sbProc = nil
        }
        if !result.hostsRemoved {
            rcLastError = "远端窗口注销报告失败，字段已按 AX 顺序清理"
            logmsg("(sjz.hud) unhost reported failure; fields cleared")
        }
        if result.pendingRemaining != 0 {
            rcLastError = "RemoteCall cleanup pending: \(result.pendingRemaining)"
            logmsg("RemoteCall cleanup pending: \(result.pendingRemaining)")
        }
    }

    private func drainSJZReaderTeardown() {
        var finished = false
        sjzWorker.async { [weak self] in
            guard let self else {
                DispatchQueue.main.async {
                    finished = true
                    CFRunLoopStop(CFRunLoopGetMain())
                }
                return
            }
            sjzesp_reset()
            self.sjzLastDiagnosticStatus = -1
            self.sjzLastDiagnosticPublishedCount = 0
            sjz_disconnect()
            DispatchQueue.main.async {
                finished = true
                CFRunLoopStop(CFRunLoopGetMain())
            }
        }
        while !finished {
            CFRunLoopRun()
        }
    }

    // A UIScene disconnect is recoverable. Invalidate every old callback and
    // release HUD/RC/memory/audio resources without setting the process-wide
    // terminal flag; a later scene may start a fresh generation.
    func disconnectSJZSceneSession() {
        guard !sjzTerminating, !sjzSceneDisconnecting else { return }
        sjzSceneDisconnecting = true
        sjzSceneEpoch &+= 1
        sjzLaunchPending = false
        sjzLaunchEpoch &+= 1
        sjzEpoch &+= 1
        sjzTimer?.cancel()
        sjzTimer = nil
        sjzHostingRequests.removeAll()
        drainSJZRemoteTeardown()
        sjzhud_request_termination_sync()
        drainSJZReaderTeardown()

        "none".withCString {
            sjzhud_set_transport_state(false, false, $0)
        }
        sjzhud_update_sjz_snapshot(nil, 0)
        sjzAttached = false
        sjzRunning = false
        sjzBase = 0
        sjzTransportName = "none"
        sjzTransportCapabilities = 0
        sjzCanWrite = false
        sjzMeasuredFPS = 0
        sjzChainDiagnostic = "场景已断开"
        sjzStatus = "场景已断开"
        sjzLastResult = ""
        sjzLastHUDText = ""
        sjzLastHUDControlFlags = UInt32.max
        sjzLastConfigFingerprint = UInt64.max
        stopBackgroundAudio()
        sjzSceneDisconnecting = false
    }

    // Process termination is permanent. Unlike a scene disconnect, all future
    // launch/audio/RemoteCall requests remain closed after this point.
    func terminateSJZSession() {
        // applicationWillTerminate@0x100007a44 is one-shot and calls remote
        // cleanup@0x1006fb8c0 before requestHUDTermination@0x100007aac.
        guard !sjzTerminating else { return }
        sjzTerminating = true
        sjzSceneEpoch &+= 1
        sjzLaunchPending = false
        sjzLaunchEpoch &+= 1
        sjzEpoch &+= 1
        sjzTimer?.cancel()
        sjzTimer = nil
        sjzHostingRequests.removeAll()
        drainSJZRemoteTeardown()
        sjzhud_request_termination_sync()
        drainSJZReaderTeardown()
        stopBackgroundAudio()
    }
    

    
    static func parsePointerOffsets(_ text: String) -> [UInt64] {
        var seps = CharacterSet.whitespacesAndNewlines
        seps.insert(charactersIn: "+,")
        let tokens = text.components(separatedBy: seps)
        var result: [UInt64] = []
        for raw in tokens {
            let token = raw.trimmingCharacters(in: .whitespacesAndNewlines)
            if token.isEmpty { continue }
            var t = token
            if t.hasPrefix("0x") || t.hasPrefix("0X") {
                t = String(t.dropFirst(2))
            }
            if let v = UInt64(t, radix: 16) {
                result.append(v)
            }
        }
        return result
    }
    

    
    func vfsinit(completion: ((Bool) -> Void)? = nil) {
        guard dsready, hasOffsets, !vfsrunning else { return }
        vfs_setlogcallback(laramgr.vfslogcallback)
        vfs_setprogresscallback { progress in
            DispatchQueue.main.async {
                laramgr.shared.vfsprogress = progress
            }
        }
        vfsattempted = true
        vfsfailed = false
        vfsrunning = true
        vfsprogress = 0.0
        
        DispatchQueue.global(qos: .userInitiated).async { [weak self] in
            let r = vfs_init()
            DispatchQueue.main.async {
                guard let self = self else { return }
                self.vfsready = (r == 0 && vfs_isready())
                if self.vfsready {
                    self.vfsfailed = false
                    self.logmsg("\nvfs 就绪！\n")
                } else {
                    self.vfsfailed = true
                    self.logmsg("\nvfs 初始化失败。\n")
                }
                self.vfsrunning = false
                self.vfsprogress = 1.0
                completion?(self.vfsready)
            }
        }
    }
    
    func sbxescape(completion: ((Bool) -> Void)? = nil) {
        guard dsready, hasOffsets, !sbxrunning else { return }
        sbxattempted = true
        sbxfailed = false
        sbxrunning = true
        
        sbx_setlogcallback(laramgr.sbxlogcallback)
        
        DispatchQueue.global(qos: .userInitiated).async { [weak self] in
            let r = sbx_escape(ds_get_our_proc())
            DispatchQueue.main.async {
                guard let self else { return }
                self.sbxready = (r == 0)
                if self.sbxready {
                    self.sbxfailed = false
                    self.logmsg("\n沙箱逃逸就绪！\n")
                } else {
                    self.sbxfailed = true
                    self.logmsg("\n沙箱逃逸失败。\n")
                }
                self.sbxrunning = false
                completion?(self.sbxready)
            }
        }
    }
    
    private static let sbxlogcallback: @convention(c) (UnsafePointer<CChar>?) -> Void = { msg in
        guard let msg = msg else { return }
        let s = String(cString: msg)
        DispatchQueue.main.async {
            laramgr.shared.logmsg("(sbx) " + s)
        }
    }
    
    private static let vfslogcallback: @convention(c) (UnsafePointer<CChar>?) -> Void = { msg in
        guard let msg = msg else { return }
        let s = String(cString: msg)
        DispatchQueue.main.async {
            laramgr.shared.vfsinitlog += "(vfs) " + s + "\n"
            laramgr.shared.logmsg("(vfs) " + s)
        }
    }
    
    func vfslistdir(path: String) -> [(name: String, isDir: Bool)]? {
        guard vfsready else {
            logmsg(" 列出目录：未就绪（\(path)）")
            return nil
        }
        var ptr: UnsafeMutablePointer<vfs_entry_t>?
        var count: Int32 = 0
        let r = vfs_listdir(path, &ptr, &count)
        guard r == 0, let entries = ptr else {
            logmsg(" 列出目录失败（\(path)）r=\(r)")
            return nil
        }
        defer { vfs_freelisting(entries) }
        
        var items: [(String, Bool)] = []
        for i in 0..<Int(count) {
            let e = entries[i]
            let name = withUnsafePointer(to: e.name) { p in
                p.withMemoryRebound(to: CChar.self, capacity: 256) { String(cString: $0) }
            }
            items.append((name, e.d_type == 4))
        }
        logmsg(" 列出目录 \(path) -> \(items.count) 项")
        return items.sorted { $0.0.lowercased() < $1.0.lowercased() }
    }
    
    func vfsread(path: String, maxSize: Int = 512 * 1024) -> Data? {
        guard vfsready else { return nil }
        let fsz = vfs_filesize(path)
        if fsz <= 0 { return nil }
        let toRead = min(Int(fsz), maxSize)
        var buf = [UInt8](repeating: 0, count: toRead)
        let n = vfs_read(path, &buf, toRead, 0)
        if n <= 0 { return nil }
        return Data(buf.prefix(Int(n)))
    }
    
    func vfswrite(path: String, data: Data) -> Bool {
        guard vfsready else { return false }
        return data.withUnsafeBytes { ptr in
            let n = vfs_write(path, ptr.baseAddress, data.count, 0)
            return n > 0
        }
    }
    
    func vfssize(path: String) -> Int64 {
        guard vfsready else { return -1 }
        return vfs_filesize(path)
    }
    
    func vfsoverwritefromlocalpath(target: String, source: String) -> Bool {
        print("(vfs) target \(source) -> \(target)")
        
        guard vfsready else {
            print("(vfs) not ready")
            return false
        }
        
        guard FileManager.default.fileExists(atPath: source) else {
            print("(vfs) 源文件未找到：\(source)")
            return false
        }
        
        let r = vfs_overwritefile(target, source)
        
        print("(vfs) vfs_overwritefile returned: \(r)")
        
        if r == 0 {
            print("(vfs) file overwritten")
        } else {
            print("(vfs) failed to overwrite file")
        }
        
        return r == 0
    }
    
    func vfsoverwritewithdata(target: String, data: Data) -> Bool {
        guard vfsready else { return false }
        let tmp = NSTemporaryDirectory() + "vfs_src_\(arc4random()).bin"
        do { try data.write(to: URL(fileURLWithPath: tmp)) } catch { return false }
        let ok = vfsoverwritefromlocalpath(target: target, source: tmp)
        try? FileManager.default.removeItem(atPath: tmp)
        return ok
    }
    
    private func sbxoverwrite(path: String, data: Data) -> (ok: Bool, message: String) {
        let immutableMessage = clearImmutableForOverwriteIfNeeded(path: path)
        let fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0o644)
        if fd == -1 {
            let prefix = immutableMessage.map { "\($0), " } ?? ""
            return (false, "\(prefix)sbx 打开失败：errno=\(errno) \(String(cString: strerror(errno)))")
        }
        defer { close(fd) }
        
        var total = 0
        let wroteAll = data.withUnsafeBytes { ptr -> Bool in
            guard let base = ptr.baseAddress else { return ptr.count == 0 }
            while total < ptr.count {
                let n = write(fd, base.advanced(by: total), ptr.count - total)
                if n <= 0 { return false }
                total += n
            }
            return true
        }
        
        if !wroteAll {
            return (false, "sbx 写入失败：errno=\(errno) \(String(cString: strerror(errno)))")
        }

        if ftruncate(fd, off_t(total)) != 0 {
            return (false, "sbx 截断失败：errno=\(errno) \(String(cString: strerror(errno)))")
        }
        
        return (true, "成功（\(total) 字节）")
    }
    
    @discardableResult
    func lara_overwritefile(target: String, source: String, fallback_vfs: Bool = true) -> (ok: Bool, message: String) {
        guard FileManager.default.fileExists(atPath: source) else {
            return (false, "源文件未找到：\(source)")
        }
        
        let result: (ok: Bool, message: String)
        if sbxready {
            do {
                let data = try Data(contentsOf: URL(fileURLWithPath: source))
                result = sbxoverwrite(path: target, data: data)
            } catch {
                result = (false, "sbx 读取源失败：\(error.localizedDescription)")
            }
        } else {
            result = (false, "sbx 未就绪")
        }
        
        if result.ok {
            return result
        }

        guard fallback_vfs else {
            return result
        }
        
        guard vfsready else {
            return (false, result.message + " | vfs 未就绪")
        }
        
        let ok = vfsoverwritefromlocalpath(target: target, source: source)
        return ok ? (true, "成功（vfs 覆盖）") : (false, result.message + " | vfs 覆盖失败")
    }
    
    @discardableResult
    func lara_overwritefile(target: String, data: Data, fallback_vfs: Bool = true) -> (ok: Bool, message: String) {
        let result = sbxready ? sbxoverwrite(path: target, data: data) : (false, "sbx 未就绪")
        if result.0 {
            return result
        }

        guard fallback_vfs else {
            return result
        }
        
        guard vfsready else {
            return (false, result.1 + ", vfs 未就绪")
        }
        
        let ok = vfsoverwritewithdata(target: target, data: data)
        return ok ? (true, "vfs 覆盖成功") : (false, result.1 + ", vfs 覆盖失败")
    }
    
    func vfszeropage(at path: String, dumb: Bool) -> Bool {
        if dumb {
            guard vfsready else {
                self.logmsg("(vfs) 清空文件失败（vfs 未就绪）")
                return false
            }
    
            let ok = path.withCString { vfs_zerofile($0) } == 0

            if !ok {
                self.logmsg("(vfs) 清空文件失败")
                return false
            }
            
            self.logmsg("(vfs) 已清空 \(path)")
            return true
        } else {
            let result = path.withCString { cpath in
                vfs_zeropage(cpath, 0)
            }

            if result != 0 {
                self.logmsg("(vfs) 清空页失败")
                return false
            }
    
            self.logmsg("(vfs) 已清空 \(path) 首页")
            return true
        }
    }
    
    func sbxgettoken(pid: Int32) -> UInt64? {
        let addr = sbx_gettoken(pid)

        guard addr != 0 else {
            return nil
        }

        return addr
    }

    func sbxgettokenstring(pid: Int32) -> String? {
        guard let cstr = sbx_copytoken(pid) else {
            return nil
        }
        defer { sbx_freestr(cstr) }
        return String(cString: cstr)
    }

    func sbxissuetoken(extClass: String, path: String) -> String? {
        guard let cstr = sbx_issue_token(extClass, path) else {
            return nil
        }
        defer { sbx_freestr(cstr) }
        return String(cString: cstr)
    }
    
    func sbxelevate() {
        DispatchQueue.main.async {
            sbx_elevate();
        }
    }
    
    func isapfs(_ path: String) -> Bool {
        var s = statfs()
        guard path.withCString({ statfs($0, &s) }) == 0 else {
            return false
        }
        
        let fstypename = s.f_fstypename
        return withUnsafePointer(to: fstypename) { ptr in
            ptr.withMemoryRebound(to: CChar.self, capacity: MemoryLayout.size(ofValue: fstypename)) {
                String(cString: $0) == "apfs"
            }
        }
    }

    // inspired by nugget from leminlimez
    func PPHelper() -> Bool {
        do {
            let fm = FileManager.default
            let dataFolder = "/private/var/mobile/Containers/Data/Application"
            let bundleFolder = "/private/var/containers/Bundle/Application"
            var bundleIDs = ["com.apple.PosterBoard"]
            if UIDevice.current.userInterfaceIdiom == .phone {
                bundleIDs.append("com.apple.CarPlayWallpaper")
            }
            guard let appList = getAppList() else { return false}
            var hashes: [String:String] = [:]
            for bundleID in bundleIDs {
                if let appInfo = appList[bundleID] {
                    hashes[bundleID] = appInfo.dataFolder
                } else {
                    // this shouldn't happen
                    logmsg("未找到 bundle ID 为 \(bundleID) 的应用。")
                    return false
                }
            }
            var PPbundleID = "com.leemin.Pocket-Poster"
            for (bundleID, info) in appList {
                if info.executable == "Pocket Poster" {
                    PPbundleID = bundleID
                    break
                } else if info.executable == "LiveContainer" {
                    PPbundleID = bundleID
                }
            }
            if let PPHash = appList[PPbundleID]?.dataFolder {
                for bundleID in hashes.keys {
                    let fileName = "Nugget" + bundleID.replacingOccurrences(of: "com.apple.", with: "") + "Hash"
                    let content = hashes[bundleID]!
                    let filePath = dataFolder + "/" + PPHash + "/Documents/" + fileName
                    try content.write(to: URL(fileURLWithPath: filePath), atomically: true, encoding: .utf8)
                    logmsg("已将哈希 \(content) 写入 \(filePath)")
                }
                return true
            } else {
                logmsg("请在使用 Pocket Poster Helper 前先安装 Pocket Poster。如果你已安装 Pocket Poster，请确认没有修改其 bundle ID。如果你把 Pocket Poster 安装在 LiveContainer 内，请同时确认没有修改 LiveContainer 的 bundle ID。")
                return false
            }
        } catch {
            logmsg("Pocket Poster Helper 出错：\(error.localizedDescription)")
            return false
        }
    }

    func getAppList() -> [String:AppInfo]? {
        let fm = FileManager.default
        let dataFolder = "/private/var/mobile/Containers/Data/Application"
        let bundleFolder = "/private/var/containers/Bundle/Application"
        var appList: [String:AppInfo] = [:]
        do {
            let appData = try fm.contentsOfDirectory(atPath: dataFolder)
            for app in appData {
                if let plist = NSDictionary(contentsOf: URL(fileURLWithPath: dataFolder + "/" + app + "/.com.apple.mobile_container_manager.metadata.plist")),
                    let bundleID = plist["MCMMetadataIdentifier"] as? String {
                    appList[bundleID] = AppInfo(executable: "", displayName: "", bundleName: "", dataFolder: app, bundleFolder: "")
                }
            }

            let appBundles = try fm.contentsOfDirectory(atPath: bundleFolder)
            for app in appBundles {
                let appPath = bundleFolder + "/" + app
                let contents = try fm.contentsOfDirectory(atPath: appPath)
                for item in contents {
                    if item.hasSuffix(".app") {
                        if let plist = NSDictionary(contentsOf: URL(fileURLWithPath: appPath + "/" + item + "/Info.plist")),
                            let bundleID = plist["CFBundleIdentifier"] as? String {
                            let executable = plist["CFBundleExecutable"] as? String ?? ""
                            let displayName = plist["CFBundleDisplayName"] as? String ?? ""
                            let bundleName = plist["CFBundleName"] as? String ?? ""
                            let dataFolderID = appList[bundleID]?.dataFolder ?? ""
                            let appInfo = AppInfo(executable: executable, displayName: displayName, bundleName: bundleName, dataFolder: dataFolderID, bundleFolder: app)
                            appList[bundleID] = appInfo
                        }
                        break
                    }
                }

            }
        } catch {
            logmsg("获取应用列表出错：\(error.localizedDescription)")
            return nil
        }
        return appList
    }
    
    func setplistvalue(path: String, key: (key: String, value: Any?), force: Bool = false) -> (ok: Bool, message: String) {
        do {
            let fm = FileManager.default
            var dict = NSMutableDictionary()
            if !fm.fileExists(atPath: path) {
                if !force { return (false, "\(path) 处的文件不存在或未找到") }
            } else {
                dict = try loadMutablePropertyListDictionary(from: URL(fileURLWithPath: path))
            }
            if let value = key.value {
                dict[key.key] = value
            } else {
                dict.removeObject(forKey: key.key)
            }
            let data = try PropertyListSerialization.data(
                fromPropertyList: dict,
                format: .binary,
                options: 0
            )
            let result = self.lara_overwritefile(
                target: path,
                data: data
            )
            if result.ok {
                return (true, "已覆盖 plist：\(path)")
            } else {
                return(false, "覆盖失败：\(result.message)")
            }
        } catch {
            return (false, "发生错误：\(error)")
        }
    }

    func getplistvalue(path: String, key: String) -> (ok: Bool, message: String, value: Any?) {
        do {
            let fm = FileManager.default
            if fm.fileExists(atPath: path) {
                let dict = try loadMutablePropertyListDictionary(from: URL(fileURLWithPath: path))
                if let value = dict[key] {
                    return (true, "成功", value)
                } else {
                    return (false, "未找到键 \(key)", nil)
                }
            } else {
                return (false, "\(path) 处的文件不存在或未找到", nil)
            }
        } catch {
            return (false, "发生错误：\(error)", nil)
        }
    }

    @discardableResult
    func apfsown(path: String, uid: UInt32, gid: UInt32) -> Bool {
        if !isapfs(path) {
            print("\(path) 是 apfs！")
        }
        
        let result = path.withCString { cPath in
            apfs_own(cPath, uid_t(uid), gid_t(gid))
        }
        
        if result != 0 {
            print("chown \(path) 失败")
            return false
        }
        
        print("已将 \(path) 的所有者改为 \(uid):\(gid)！")
        return true
    }
    
    #if !DISABLE_REMOTECALL
    func rcinit(process: String, migbypass: Bool = false, completion: ((Bool) -> Void)? = nil) {
        guard dsready, !sjzTerminating, !sjzSceneDisconnecting else {
            completion?(false)
            return
        }
        let sceneEpoch = sjzSceneEpoch
        if rcready {
            completion?(sbProc != nil)
            return
        }
        guard !rcrunning else {
            completion?(false)
            return
        }
        
        rcrunning = true
        rcLastError = nil
        logmsg("正在初始化远程调用 \(process)...")
        
        DispatchQueue.global(qos: .userInitiated).async { [weak self] in
            let remoteProcess = RemoteCall(
                process: process,
                useMigFilterBypass: migbypass
            )
            
            DispatchQueue.main.async {
                guard let self = self else { return }
                if self.sjzTerminating || self.sjzSceneDisconnecting ||
                    self.sjzSceneEpoch != sceneEpoch {
                    self.sjzWorker.async { [weak self, remoteProcess] in
                        guard let self, let remoteProcess else { return }
                        _ = self.destroyRemoteCallOnWorker(remoteProcess)
                        for pendingProcess in Array(self.sjzPendingRemoteCleanup.values) {
                            _ = self.destroyRemoteCallOnWorker(pendingProcess)
                        }
                    }
                    self.rcrunning = false
                    completion?(false)
                    return
                }
                self.sbProc = remoteProcess
                let success = remoteProcess != nil
                if success {
                    self.logmsg("远程调用已在 \(process) 上初始化")
                    self.rcLastError = nil
                    self.rcrunning = false
                    self.rcready = true
                } else {
                    self.logmsg("远程调用初始化失败 \(process)")
                    let error = RemoteCall.lastInitError()
                    self.rcLastError = error
                    if let error, !error.isEmpty {
                        self.logmsg("远程调用初始化失败 \(process)：\(error)")
                    } else {
                        self.logmsg("远程调用初始化失败 \(process)")
                    }
                    self.rcrunning = false
                }
                completion?(success)
            }
        }
    }
    
    func rcinitDaemon(serviceName: String, framework: String? = nil, process: String, migbypass: Bool = false, completion: ((RemoteCall?) -> Void)? = nil) {
        guard dsready, let sbProc else {
            completion?(nil)
            return
        }
        
        rcrunning = true
        logmsg("正在初始化远程调用 \(process)...")
        
        DispatchQueue.global(qos: .userInitiated).async { [weak self] in
            if process.withCString({ proc_find_by_name($0) == 0 }) {
                wake_up_daemon(sbProc, serviceName, framework)
                sleep(1) // give the daemon some time to start up
            }
            
            let proc = RemoteCall(process: process, useMigFilterBypass: migbypass)
            completion?(proc)
            
            DispatchQueue.main.async {
                guard let self = self else { return }
                let success = proc != nil
                if success {
                    self.logmsg("远程调用已在 \(process) 上初始化")
                    self.rcrunning = false
                } else {
                    let error = RemoteCall.lastInitError()
                    if let error, !error.isEmpty {
                        self.logmsg("远程调用初始化失败 \(process)：\(error)")
                    } else {
                        self.logmsg("远程调用初始化失败 \(process)")
                    }
                    self.rcrunning = false
                }
            }
        }
    }
    
    func rcdestroy(completion: (() -> Void)? = nil) {
        logmsg("正在销毁远程调用会话...")
        sjzLaunchEpoch &+= 1
        sjzGameHUDSessionArmed = false
        sjzGameHUDEnabled = false
        rcrunning = true
        let remoteProcess = sbProc
        // AX remote cleanup@0x1006fb8c0 serializes this block. The wrapper at
        // 0x1006ffba8 keeps a failed destroy in its deduplicated pending set.
        sjzWorker.async { [weak self, remoteProcess] in
            guard let self else { return }
            let result = self.cleanupRemoteCallsOnWorker(remoteProcess)
            
            DispatchQueue.main.async {
                self.rcrunning = false
                if self.sbProc === remoteProcess {
                    self.rcready = false
                    self.sbProc = nil
                }
                if !result.hostsRemoved {
                    self.rcLastError = "远端窗口注销报告失败，字段已按 AX 顺序清理"
                    self.logmsg("(sjz.hud) unhost reported failure; fields cleared")
                }
                if result.pendingRemaining != 0 {
                    self.rcLastError = "RemoteCall cleanup pending: \(result.pendingRemaining)"
                    self.logmsg("RemoteCall cleanup pending: \(result.pendingRemaining)")
                }
                self.sjzGameHUDActive = false
                sjzhud_request_termination_sync()
                self.logmsg(result.currentDestroyed
                    ? "远程调用会话已销毁"
                    : "远程调用会话等待重试")
                completion?()
            }
        }
    }

    func stashKRWToLaunchd(completion: ((Bool) -> Void)? = nil) {
        guard dsready, !rcrunning else {
            completion?(false)
            return
        }

        rcrunning = true
        rcLastError = nil
        logmsg("(persist) 正在手动转移 KRW 原语到 launchd...")

        DispatchQueue.global(qos: .userInitiated).async { [weak self] in
            let success = transfer_krw_to_launchd()

            DispatchQueue.main.async {
                guard let self else { return }
                self.rcrunning = false
                if success {
                    self.rcLastError = nil
                    self.logmsg("(persist) 手动转移 KRW 原语到 launchd 成功")
                } else {
                    let error = RemoteCall.lastInitError()
                    self.rcLastError = error
                    if let error, !error.isEmpty {
                        self.logmsg("(persist) 手动转移 KRW 原语到 launchd 失败：\(error)")
                    } else {
                        self.logmsg("(persist) 手动转移 KRW 原语到 launchd 失败")
                    }
                }
                completion?(success)
            }
        }
    }
    
    //  params:
    //  - name: function to call
    //  - args: up to 8 args in registers (x0-x7) and extra args passed to stack pointer
    //  - timeout: timeout in ms
    //  ret: return value from rc
    func rccall(name: String, args: [UInt64] = [], timeout: Int32 = 100) -> UInt64 {
        guard rcready else { return 0 }
        let RTLD_DEFAULT = UnsafeMutableRawPointer(bitPattern: -2)
        let ptr = dlsym(RTLD_DEFAULT, name)
        var argsCopy = args
        return name.withCString { (cName: UnsafePointer<CChar>) -> UInt64 in
            UInt64(argsCopy.withUnsafeMutableBufferPointer { buffer in
                sbProc?.doStable(
                    withTimeout: timeout,
                    functionName: UnsafeMutablePointer(mutating: cName),
                    functionPointer: ptr,
                    args: buffer.baseAddress,
                    argCount: UInt(args.count)
                ) ?? 0
            })
        }
    }
    #endif
}
