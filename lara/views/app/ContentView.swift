import Combine
import UIKit

final class SJZLauncherViewController: UIViewController {
    private let manager: laramgr
    private var subscriptions = Set<AnyCancellable>()
    private let gradient = CAGradientLayer()
    private let status = UILabel()
    private let detail = UILabel()
    private let connectionBadge = UILabel()
    private let connectionDot = UIView()
    private let launch = UIButton(type: .system)

    init(manager: laramgr) {
        self.manager = manager
        super.init(nibName: nil, bundle: nil)
    }

    required init?(coder: NSCoder) {
        fatalError("init(coder:) has not been implemented")
    }

    private func rgb(_ red: CGFloat, _ green: CGFloat, _ blue: CGFloat) -> UIColor {
        UIColor(red: red / 255, green: green / 255, blue: blue / 255, alpha: 1)
    }

    private func text(_ value: String, size: CGFloat, weight: UIFont.Weight,
                      color: UIColor) -> UILabel {
        let label = UILabel()
        label.text = value
        label.textColor = color
        label.font = .systemFont(ofSize: size, weight: weight)
        return label
    }

    private func styleAction(_ button: UIButton, title: String, symbol: String,
                             tag: Int, primary: Bool) {
        button.tag = tag
        button.setTitle(title, for: .normal)
        button.setImage(UIImage(systemName: symbol), for: .normal)
        button.titleLabel?.font = .systemFont(ofSize: primary ? 17 : 14, weight: .semibold)
        button.tintColor = primary ? rgb(5, 35, 42) : rgb(226, 238, 241)
        button.setTitleColor(primary ? rgb(5, 35, 42) : rgb(226, 238, 241), for: .normal)
        button.backgroundColor = primary ? rgb(52, 192, 203) : rgb(24, 43, 52)
        button.layer.cornerRadius = 15
        if !primary {
            button.layer.borderWidth = 1
            button.layer.borderColor = rgb(49, 77, 88).cgColor
        }
        button.heightAnchor.constraint(equalToConstant: primary ? 56 : 50).isActive = true
        button.addTarget(self, action: #selector(performAction(_:)), for: .touchUpInside)
        button.imageEdgeInsets = UIEdgeInsets(top: 0, left: -7, bottom: 0, right: 7)
    }

    override func viewDidLoad() {
        super.viewDidLoad()
        view.backgroundColor = rgb(10, 21, 28)
        gradient.colors = [rgb(18, 42, 51).cgColor, rgb(10, 21, 28).cgColor]
        gradient.startPoint = CGPoint(x: 0, y: 0)
        gradient.endPoint = CGPoint(x: 1, y: 1)
        view.layer.insertSublayer(gradient, at: 0)

        let scroll = UIScrollView()
        scroll.translatesAutoresizingMaskIntoConstraints = false
        scroll.alwaysBounceVertical = true
        view.addSubview(scroll)
        NSLayoutConstraint.activate([
            scroll.topAnchor.constraint(equalTo: view.safeAreaLayoutGuide.topAnchor),
            scroll.bottomAnchor.constraint(equalTo: view.safeAreaLayoutGuide.bottomAnchor),
            scroll.leadingAnchor.constraint(equalTo: view.leadingAnchor),
            scroll.trailingAnchor.constraint(equalTo: view.trailingAnchor)
        ])

        let stack = UIStackView()
        stack.axis = .vertical
        stack.alignment = .fill
        stack.translatesAutoresizingMaskIntoConstraints = false
        scroll.addSubview(stack)
        let preferredWidth = stack.widthAnchor.constraint(
            equalTo: scroll.frameLayoutGuide.widthAnchor, constant: -48)
        preferredWidth.priority = .defaultHigh
        NSLayoutConstraint.activate([
            stack.topAnchor.constraint(equalTo: scroll.contentLayoutGuide.topAnchor, constant: 32),
            stack.bottomAnchor.constraint(equalTo: scroll.contentLayoutGuide.bottomAnchor, constant: -34),
            stack.centerXAnchor.constraint(equalTo: scroll.frameLayoutGuide.centerXAnchor),
            scroll.contentLayoutGuide.widthAnchor.constraint(equalTo: scroll.frameLayoutGuide.widthAnchor),
            stack.widthAnchor.constraint(lessThanOrEqualToConstant: 440),
            preferredWidth
        ])
        func add(_ item: UIView, gap: CGFloat) {
            stack.addArrangedSubview(item)
            stack.setCustomSpacing(gap, after: item)
        }

        let brand = UIStackView()
        brand.axis = .horizontal
        brand.alignment = .center
        brand.spacing = 12
        let mark = UIView()
        mark.backgroundColor = rgb(52, 192, 203)
        mark.layer.cornerRadius = 12
        mark.translatesAutoresizingMaskIntoConstraints = false
        mark.widthAnchor.constraint(equalToConstant: 42).isActive = true
        mark.heightAnchor.constraint(equalToConstant: 42).isActive = true
        let delta = text("Δ", size: 27, weight: .bold, color: rgb(5, 35, 42))
        delta.textAlignment = .center
        delta.translatesAutoresizingMaskIntoConstraints = false
        mark.addSubview(delta)
        NSLayoutConstraint.activate([
            delta.centerXAnchor.constraint(equalTo: mark.centerXAnchor),
            delta.centerYAnchor.constraint(equalTo: mark.centerYAnchor)
        ])
        brand.addArrangedSubview(mark)
        let brandWords = UIStackView()
        brandWords.axis = .vertical
        brandWords.spacing = 2
        brandWords.addArrangedSubview(text("SJZ OVERLAY", size: 13, weight: .bold,
                                           color: rgb(231, 245, 247)))
        brandWords.addArrangedSubview(text("DELTA FORCE · CONTROL", size: 10,
                                           weight: .medium, color: rgb(124, 158, 170)))
        brand.addArrangedSubview(brandWords)
        add(brand, gap: 37)

        add(text("TACTICAL DISPLAY", size: 11, weight: .semibold,
                 color: rgb(52, 192, 203)), gap: 8)
        add(text("三角洲行动", size: 34, weight: .bold,
                 color: .white), gap: 9)
        let subtitle = text("人物、物资与战场信息，一处查看。", size: 14,
                            weight: .regular, color: rgb(160, 182, 190))
        subtitle.numberOfLines = 0
        add(subtitle, gap: 29)

        let card = UIView()
        card.backgroundColor = rgb(22, 41, 50)
        card.layer.cornerRadius = 19
        card.layer.borderWidth = 1
        card.layer.borderColor = rgb(49, 77, 88).cgColor
        let cardStack = UIStackView()
        cardStack.axis = .vertical
        cardStack.spacing = 13
        cardStack.layoutMargins = UIEdgeInsets(top: 18, left: 18, bottom: 18, right: 18)
        cardStack.isLayoutMarginsRelativeArrangement = true
        cardStack.translatesAutoresizingMaskIntoConstraints = false
        card.addSubview(cardStack)
        NSLayoutConstraint.activate([
            cardStack.topAnchor.constraint(equalTo: card.topAnchor),
            cardStack.bottomAnchor.constraint(equalTo: card.bottomAnchor),
            cardStack.leadingAnchor.constraint(equalTo: card.leadingAnchor),
            cardStack.trailingAnchor.constraint(equalTo: card.trailingAnchor)
        ])
        let cardHead = UIStackView()
        cardHead.axis = .horizontal
        cardHead.alignment = .center
        cardHead.spacing = 7
        connectionDot.backgroundColor = rgb(129, 148, 158)
        connectionDot.layer.cornerRadius = 4
        connectionDot.widthAnchor.constraint(equalToConstant: 8).isActive = true
        connectionDot.heightAnchor.constraint(equalToConstant: 8).isActive = true
        cardHead.addArrangedSubview(connectionDot)
        cardHead.addArrangedSubview(text("连接状态", size: 12, weight: .semibold,
                                          color: rgb(151, 176, 185)))
        cardHead.addArrangedSubview(UIView())
        connectionBadge.font = .systemFont(ofSize: 11, weight: .semibold)
        cardHead.addArrangedSubview(connectionBadge)
        cardStack.addArrangedSubview(cardHead)
        let separator = UIView()
        separator.backgroundColor = rgb(49, 77, 88)
        separator.heightAnchor.constraint(equalToConstant: 1).isActive = true
        cardStack.addArrangedSubview(separator)
        status.font = .systemFont(ofSize: 16, weight: .semibold)
        status.textColor = .white
        status.numberOfLines = 0
        cardStack.addArrangedSubview(status)
        detail.font = .systemFont(ofSize: 12)
        detail.textColor = rgb(151, 176, 185)
        detail.numberOfLines = 0
        cardStack.addArrangedSubview(detail)
        add(card, gap: 26)

        add(text("快捷操作", size: 12, weight: .semibold,
                 color: rgb(124, 158, 170)), gap: 10)
        styleAction(launch, title: "启动三角洲", symbol: "play.fill", tag: 1,
                    primary: true)
        add(launch, gap: 11)
        let secondary = UIStackView()
        secondary.axis = .horizontal
        secondary.distribution = .fillEqually
        secondary.spacing = 10
        let initialize = UIButton(type: .system)
        styleAction(initialize, title: "初始化环境", symbol: "gearshape.2",
                    tag: 0, primary: false)
        secondary.addArrangedSubview(initialize)
        let panel = UIButton(type: .system)
        styleAction(panel, title: "悬浮面板", symbol: "rectangle.on.rectangle",
                    tag: 2, primary: false)
        secondary.addArrangedSubview(panel)
        add(secondary, gap: 18)

        let disconnect = UIButton(type: .system)
        disconnect.tag = 3
        disconnect.setTitle("断开连接", for: .normal)
        disconnect.setTitleColor(rgb(164, 184, 192), for: .normal)
        disconnect.titleLabel?.font = .systemFont(ofSize: 13, weight: .medium)
        disconnect.contentHorizontalAlignment = .left
        disconnect.heightAnchor.constraint(equalToConstant: 30).isActive = true
        disconnect.addTarget(self, action: #selector(performAction(_:)), for: .touchUpInside)
        add(disconnect, gap: 27)
        add(text("SJZ OVERLAY  ·  1.201.37117", size: 10,
                 weight: .medium, color: rgb(101, 132, 143)), gap: 0)

        manager.objectWillChange.receive(on: DispatchQueue.main).sink { [weak self] _ in
            DispatchQueue.main.async { self?.refresh() }
        }.store(in: &subscriptions)
        refresh()
    }

    override func viewDidLayoutSubviews() {
        super.viewDidLayoutSubviews()
        gradient.frame = view.bounds
    }

    private func refresh() {
        status.text = manager.sjzStatus
        detail.text = manager.sjzAttached ? manager.sjzChainDiagnostic : manager.sjzGameHUDStatus
        let connected = manager.sjzAttached
        connectionBadge.text = connected ? "已连接" : manager.sjzRunning ? "连接中" : "未连接"
        connectionBadge.textColor = connected ? rgb(80, 211, 183) : rgb(160, 182, 190)
        connectionDot.backgroundColor = connected ? rgb(80, 211, 183) : rgb(129, 148, 158)
        launch.isEnabled = !manager.dsrunning && !manager.sjzRunning
        launch.alpha = launch.isEnabled ? 1 : 0.55
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
