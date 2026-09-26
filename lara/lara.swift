//
//  lara.swift
//  lara
//
//  Created by ruter on 23.03.26.
//

import UIKit
import UniformTypeIdentifiers

let g_isunsupported: Bool = isunsupported()
var weonadebugbuild_pjbweouttahereexclamationmark: Bool = false

@main
final class LaraAppDelegate: UIResponder, UIApplicationDelegate {
    func application(
        _ application: UIApplication,
        didFinishLaunchingWithOptions launchOptions: [UIApplication.LaunchOptionsKey: Any]? = nil
    ) -> Bool {
        laramgr.shared.startBackgroundAudio()
        bootstrapLaraApplication()
        return true
    }

    func applicationWillTerminate(_ application: UIApplication) {
        laramgr.shared.terminateSJZSession()
    }

    func application(
        _ application: UIApplication,
        configurationForConnecting connectingSceneSession: UISceneSession,
        options: UIScene.ConnectionOptions
    ) -> UISceneConfiguration {
        let configuration = UISceneConfiguration(
            name: "Default Configuration",
            sessionRole: connectingSceneSession.role
        )
        configuration.delegateClass = LaraSceneDelegate.self
        return configuration
    }

    func application(
        _ application: UIApplication,
        supportedInterfaceOrientationsFor window: UIWindow?
    ) -> UIInterfaceOrientationMask {
        .portrait
    }
}

@objc(ZeqcgKhNvh)
final class LaraSceneDelegate: UIResponder, UIWindowSceneDelegate {
    var window: UIWindow?

    func scene(
        _ scene: UIScene,
        willConnectTo session: UISceneSession,
        options connectionOptions: UIScene.ConnectionOptions
    ) {
        guard let windowScene = scene as? UIWindowScene else { return }
        let window = UIWindow(windowScene: windowScene)
        self.window = window
        window.backgroundColor = .black
        window.rootViewController = SJZLauncherViewController(manager: laramgr.shared)
        window.makeKeyAndVisible()
    }

    func sceneDidBecomeActive(_ scene: UIScene) {
    }

    func sceneWillEnterForeground(_ scene: UIScene) {
    }

    func sceneWillResignActive(_ scene: UIScene) {
    }

    func sceneDidEnterBackground(_ scene: UIScene) {
    }

    func sceneDidDisconnect(_ scene: UIScene) {
    }
}

private func bootstrapLaraApplication() {
    #if DEBUG
    weonadebugbuild_pjbweouttahereexclamationmark = true
    #endif

    // AX AppDelegate notification registration@0x100007470 listens for the
    // exact Darwin request used by requestHUDTermination@0x100007aac.
    sjzhud_install_termination_notification()

    // fix file picker
    let fixMethod = class_getInstanceMethod(
        UIDocumentPickerViewController.self,
        #selector(UIDocumentPickerViewController.fix_init(forOpeningContentTypes:asCopy:))
    )!
    let origMethod = class_getInstanceMethod(
        UIDocumentPickerViewController.self,
        #selector(UIDocumentPickerViewController.init(forOpeningContentTypes:asCopy:))
    )!
    method_exchangeImplementations(origMethod, fixMethod)

    if UserDefaults.standard.bool(forKey: "keepAlive") {
        toggleka()
    }
    globallogger.capture()
}

// file picker fixes
extension UIDocumentPickerViewController {
    @objc func fix_init(forOpeningContentTypes contentTypes: [UTType], asCopy: Bool) -> UIDocumentPickerViewController {
        return fix_init(forOpeningContentTypes: contentTypes, asCopy: true)
    }
}

// make strings compatiable with errors
extension String: @retroactive Error {}
