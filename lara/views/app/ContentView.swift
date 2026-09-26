import Combine
import UIKit

final class SJZLauncherViewController: UIViewController {
    private let manager: laramgr
    private var subscriptions=Set<AnyCancellable>()
    private let status=UILabel()
    private let detail=UILabel()
    private let launch=UIButton(type:.system)
    init(manager: laramgr) { self.manager=manager; super.init(nibName:nil,bundle:nil) }
    required init?(coder: NSCoder) { fatalError("init(coder:) has not been implemented") }
    override func viewDidLoad() {
        super.viewDidLoad()
        view.backgroundColor=UIColor(red:0.07,green:0.10,blue:0.12,alpha:1)
        let stack=UIStackView(); stack.axis = .vertical; stack.spacing=18
        stack.translatesAutoresizingMaskIntoConstraints=false
        view.addSubview(stack)
        NSLayoutConstraint.activate([
            stack.leadingAnchor.constraint(equalTo:view.safeAreaLayoutGuide.leadingAnchor,constant:28),
            stack.trailingAnchor.constraint(equalTo:view.safeAreaLayoutGuide.trailingAnchor,constant:-28),
            stack.centerYAnchor.constraint(equalTo:view.centerYAnchor)
        ])
        let title=UILabel(); title.text="三角洲行动"; title.textColor = .white
        title.font = .boldSystemFont(ofSize:30)
        stack.addArrangedSubview(title)
        let subtitle=UILabel(); subtitle.text="人物 · 物资 · 悬浮面板"
        subtitle.textColor = .lightGray; stack.addArrangedSubview(subtitle)
        status.numberOfLines=0; status.textColor = .white; status.font = .systemFont(ofSize:16)
        detail.numberOfLines=0; detail.textColor = .lightGray; detail.font = .systemFont(ofSize:13)
        stack.addArrangedSubview(status); stack.addArrangedSubview(detail)
        let labels=["初始化环境","启动三角洲","打开悬浮面板","断开连接"]
        for i in labels.indices {
            let button=i==1 ? launch : UIButton(type:.system)
            button.tag=i; button.setTitle(labels[i],for:.normal)
            button.titleLabel?.font = .boldSystemFont(ofSize:17)
            button.backgroundColor=UIColor(red:0.12,green:0.35,blue:0.31,alpha:1)
            button.tintColor = .white; button.layer.cornerRadius=12
            button.heightAnchor.constraint(equalToConstant:50).isActive=true
            button.addTarget(self,action:#selector(performAction(_:)),for:.touchUpInside)
            stack.addArrangedSubview(button)
        }
        manager.objectWillChange.receive(on:DispatchQueue.main).sink { [weak self] _ in
            DispatchQueue.main.async { self?.refresh() }
        }.store(in:&subscriptions)
        refresh()
    }
    private func refresh() {
        status.text=manager.sjzStatus
        detail.text=manager.sjzAttached ? manager.sjzChainDiagnostic : manager.sjzGameHUDStatus
        launch.isEnabled = !manager.dsrunning && !manager.sjzRunning
        launch.alpha=launch.isEnabled ? 1 : 0.5
    }
    @objc private func performAction(_ sender: UIButton) {
        switch sender.tag {
        case 0: manager.initializeSJZEnvironment()
        case 1: manager.launchSJZGame()
        case 2: manager.openSJZControlPanel()
        case 3:
            manager.sjzDetach()
            manager.setGameHUD(false)
        default: break
        }
    }
}
