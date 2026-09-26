#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
ROOT="$(pwd)"
CONFIG=Release
PACKAGE=0
for arg in "$@"; do
    case "$arg" in
        --debug) CONFIG=Debug ;;
        --package) PACKAGE=1 ;;
        *) printf '未知参数：%s（可用 --debug、--package）\n' "$arg" >&2; exit 2 ;;
    esac
done
say() { printf '[*] %s\n' "$*"; }
ok() { printf '[+] %s\n' "$*"; }
die() { printf '[!] %s\n' "$*" >&2; exit 1; }
for tool in xcodebuild xcrun python3 git; do
    command -v "$tool" >/dev/null || die "缺少 $tool；需要 macOS/Xcode"
done
mkdir -p "$ROOT/build"
reset_build_dir() {
    local target="$1"
    case "$target" in "$ROOT"/build/*) ;; *) die "拒绝清理工程 build 外路径" ;; esac
    [[ "$target" != "$ROOT/build/" ]] || die "拒绝清理整个 build"
    rm -rf -- "$target"
    mkdir -p "$target"
}
# ── 从源码构建 libxpf.dylib ──────────────────────────────────────────────────
# lara/lib/libxpf.dylib 曾经是一个提交进 git 的预编译二进制（2026-09-05），
# 早于 XPF 的 "Fix some metrics not working on higher versions of iOS 26 and on
# iOS 27 betas" 提交。旧版没有 arm_maxoffset 这条 fallback，于是
# xpf_find_pmap_bootstrap 的字符串查找失败、XPF_ASSERT 直接终止，
# pointer_mask 与 T1SZ_BOOT 都拿不到 —— 内核注入层的 call primitive 与
# task port 随之全部失效。这里改为每次构建都从 vendor/XPF 源码编译，
# 保证「修好的源码」真的进入出货二进制。
say "从源码构建并静态链接 XPF / libgrabkernel2 ..."
XPF_DIR="$ROOT/vendor/XPF"
[ -f "$XPF_DIR/src/common.c" ] || die "缺少 vendor/XPF/src"
[ -f "$XPF_DIR/Makefile" ]     || die "缺少 vendor/XPF/Makefile"
# AX 1.2.8 的入口只接收 kernelcache。Lara 侧同样只传一个参数，因此在编译
# 出货 dylib 前强制核对声明、实现和全部调用，禁止再次混入三参数 ABI。
XPF_SINGLE_DECL='int xpf_start_with_kernel_path(const char *kernelPath);'
LC_ALL=C grep -Fqx -- "$XPF_SINGLE_DECL" "$ROOT/lara/headers/xpf.h" \
    || die "Lara 的 XPF 声明不是 AX 1.2.8 单参数 ABI"
LC_ALL=C grep -Fqx -- "$XPF_SINGLE_DECL" "$XPF_DIR/src/xpf.h" \
    || die "vendor/XPF 的公开声明不是 AX 1.2.8 单参数 ABI"
LC_ALL=C grep -Fqx -- 'int xpf_start_with_kernel_path(const char *kernelPath)' "$XPF_DIR/src/xpf.c" \
    || die "vendor/XPF 的实现不是 AX 1.2.8 单参数 ABI"
if LC_ALL=C grep -nE 'xpf_start_with_kernel_path[[:space:]]*\([^)]*,' \
        "$ROOT/lara/headers/xpf.h" \
        "$ROOT/lara/kexploit/offsets.m" \
        "$ROOT/lara/kexploit/utils.m" \
        "$XPF_DIR/src/xpf.h" \
        "$XPF_DIR/src/xpf.c" \
        "$XPF_DIR/src/cli/main.c"; then
    die "检测到多参数 xpf_start_with_kernel_path，拒绝构建 ABI 混用产物"
fi
# Lara 会直接读取导出全局 gXPF 的字段；两份头文件的结构体必须逐字段一致。
# 2026-09-20 曾因 Lara 仍把 firstItem 当成 +0x110、而 dylib 已移到 +0x1a8，
# 将 kernelSandboxAuthStubSection 误作链表头并在 Mach-O 魔数地址上崩溃。
python3 - "$ROOT/lara/headers/xpf.h" "$XPF_DIR/src/xpf.h" <<'PY' \
    || die "Lara 与 vendor/XPF 的 gXPF 结构布局不一致"
import pathlib
import re
import sys

def struct_body(path):
    text = pathlib.Path(path).read_text(encoding="utf-8")
    match = re.search(r"typedef\s+struct\s+s_XPF\s*\{(.*?)\}\s*XPF\s*;", text, re.S)
    if not match:
        raise SystemExit(f"找不到 XPF 结构体: {path}")
    body = re.sub(r"/\*.*?\*/|//[^\r\n]*", "", match.group(1), flags=re.S)
    return re.sub(r"\s+", " ", body).strip()

if struct_body(sys.argv[1]) != struct_body(sys.argv[2]):
    raise SystemExit("gXPF layout mismatch")

for path in sys.argv[1:]:
    text = pathlib.Path(path).read_text(encoding="utf-8")
    for forbidden in (
        "kernelBootcodeSection", "kernelSandboxAuthStubSection",
        "kernelIOSurfaceTextSection", "kernelIOSurfaceStringSection",
        "kernelIOSurfaceOsLogSection", "decompressedSptm", "decompressedTxm",
        "sptmContainer", "txmContainer",
    ):
        if forbidden in text:
            raise SystemExit(f"forbidden AX 1.2.8 XPF field {forbidden}: {path}")
    for required in (
        "offsetof(XPF, firstItem) == 0x110",
        "offsetof(XPF, ignoreBaseSet) == 0x118",
        "sizeof(XPF) == 0x120",
    ):
        if required not in text:
            raise SystemExit(f"missing ABI assertion {required}: {path}")
PY
for removed in "$XPF_DIR/src/sptm_txm.c" "$XPF_DIR/src/sptm_txm.h" \
               "$XPF_DIR/src/im4p_direct.c" "$XPF_DIR/src/im4p_direct.h"; do
    [[ ! -e "$removed" ]] || die "旧 XPF 构建仍混入可选镜像源：$removed"
done
if LC_ALL=C grep -R -nE 'xpf_sptm_txm_init|decompressedSptm|decompressedTxm|kernelBootcodeSection|kernelSandboxAuthStubSection' \
        "$XPF_DIR/src" "$XPF_DIR/Makefile"; then
    die "旧 XPF 生产链仍混入 SPTM/TXM 初始化或新版 section"
fi
LC_ALL=C grep -q -- 'arm_maxoffset' "$XPF_DIR/src/common.c" \
    || die "旧 XPF 源码缺少 arm_maxoffset 兼容 finder"

verify_xpf_binary_layout() {
    local artifact="$1"
    shift
    python3 - "$artifact" "$@" <<'PY' \
        || die "XPF 产物 ABI 偏移验证失败：$artifact"
import pathlib
import re
import subprocess
import sys

artifact = pathlib.Path(sys.argv[1])
required_archs = sys.argv[2:]
if not artifact.is_file():
    raise SystemExit(f"missing artifact: {artifact}")
if not required_archs:
    raise SystemExit("no required architecture supplied")

archs = subprocess.check_output(
    ["xcrun", "lipo", "-archs", str(artifact)], text=True
).split()
for required_arch in required_archs:
    if required_arch not in archs:
        raise SystemExit(f"missing architecture {required_arch}: {artifact}")

required_offsets = {
    "_xpf_item_register": "0x110",
    "_xpf_item_resolve": "0x110",
    "_xpf_set_ignore_base_set": "0x118",
}
for arch in required_archs:
    symbols = subprocess.check_output(
        ["xcrun", "nm", "-arch", arch, "-n", str(artifact)], text=True
    )
    gxpf_match = re.search(
        r"(?mi)^([0-9a-f]+)\s+\S\s+_gXPF\s*$", symbols
    )
    if not gxpf_match:
        raise SystemExit(f"missing _gXPF symbol ({arch}): {artifact}")
    gxpf_address = int(gxpf_match.group(1), 16)
    disassembly = subprocess.check_output(
        ["xcrun", "otool", "-arch", arch, "-tvV", str(artifact)], text=True
    )
    for symbol, offset in required_offsets.items():
        match = re.search(
            rf"(?ms)^(?:[0-9a-f]+\s+)?{re.escape(symbol)}:\s*\n"
            rf"(.*?)(?=^(?:[0-9a-f]+\s+)?_\S*:\s*\n|\Z)",
            disassembly,
        )
        if not match:
            raise SystemExit(f"missing symbol {symbol} ({arch}): {artifact}")
        offset_value = int(offset, 16)
        # Clang may either materialize &gXPF first and keep the member offset in
        # the load/store, or fold gXPF's page offset into that displacement.
        displacements = {offset_value, (gxpf_address & 0xfff) + offset_value}
        operand_patterns = [
            rf"(?<![0-9a-f])#(?:0x{value:x}|{value})(?![0-9a-f])"
            for value in displacements
        ]
        if not any(
            re.search(pattern, match.group(1), re.IGNORECASE)
            for pattern in operand_patterns
        ):
            raise SystemExit(
                f"{symbol} does not access gXPF + {offset} ({arch}): {artifact}"
            )

binary = artifact.read_bytes()
for forbidden in (b"xpf_sptm_txm_init", b"decompressedSptm", b"decompressedTxm"):
    if forbidden in binary:
        raise SystemExit(f"forbidden optional-image marker {forbidden!r}: {artifact}")
PY
}

CHOMA_COMMIT=b1a4f2debf2aff70edc2825c5cfbd05926d7fc18
CHOMA_DIR="$ROOT/build/deps/ChOma"
if [ ! -d "$CHOMA_DIR/.git" ]; then
    [[ ! -e "$CHOMA_DIR" ]] || die "ChOma 依赖目录存在但不是 Git checkout"
    say "拉取固定版本 ChOma ..."
    mkdir -p "$(dirname "$CHOMA_DIR")"
    git clone --no-checkout https://github.com/opa334/ChOma \
        "$CHOMA_DIR" >/dev/null 2>&1 || die "无法拉取 ChOma"
fi
git -C "$CHOMA_DIR" fetch --depth 1 origin "$CHOMA_COMMIT" >/dev/null 2>&1 \
    || die "无法获取固定 ChOma 提交 $CHOMA_COMMIT"
git -C "$CHOMA_DIR" checkout --detach "$CHOMA_COMMIT" >/dev/null 2>&1 \
    || die "无法切换到固定 ChOma 提交 $CHOMA_COMMIT"
[[ "$(git -C "$CHOMA_DIR" rev-parse HEAD)" == "$CHOMA_COMMIT" ]] \
    || die "ChOma 版本不一致"

# 这里会实际编译头文件中的三条 ABI 断言；失败信息分别包含
# "AX 1.2.8 firstItem ABI"、ignoreBaseSet ABI 和 XPF size。
IOS_SDK="$(xcrun --sdk iphoneos --show-sdk-path)"
xcrun --sdk iphoneos clang -fsyntax-only -arch arm64 -isysroot "$IOS_SDK" \
    -DXPF_LAYOUT_ONLY "$ROOT/tests/xpf_ax128_layout_test.c" \
    || die "XPF AX 1.2.8 布局编译门禁失败"
xcrun --sdk iphoneos clang -fsyntax-only -arch arm64 -isysroot "$IOS_SDK" \
    -DXPF_LAYOUT_ONLY -DXPF_TEST_LARA_HEADER "$ROOT/tests/xpf_ax128_layout_test.c" \
    || die "Lara XPF AX 1.2.8 布局编译门禁失败"
mkdir -p "$ROOT/build"
# 该 dylib 只用于 lipo/nm/otool ABI 门禁，不进入 App；覆盖 XPF Makefile 的
# 可选签名器，避免为这个一次性检查产物引入 Homebrew ldid 依赖。
if ! make -B -C "$XPF_DIR" output/ios/libxpf.dylib CHOMA_PATH="$CHOMA_DIR" \
        LDID=/usr/bin/true \
        >"$ROOT/build/xpf-build.log" 2>&1; then
    tail -40 "$ROOT/build/xpf-build.log" >&2
    die "libxpf 编译失败，见 build/xpf-build.log"
fi
verify_xpf_binary_layout "$XPF_DIR/output/ios/libxpf.dylib" arm64 arm64e

# 不将上面的 ABI 验证 dylib 复制到 App。生产链重新以 arm64e / 16.5.1
# 编译同一份 XPF + ChOma 源码，并通过 -force_load 并入主 Mach-O。
STATIC_DIR="$ROOT/build/static-ios"
reset_build_dir "$STATIC_DIR"
mkdir -p "$STATIC_DIR/obj/xpf" "$STATIC_DIR/obj/grabkernel"

xpf_sources=(
    "$XPF_DIR/src/bad_recovery.c"
    "$XPF_DIR/src/common.c"
    "$XPF_DIR/src/decompress.c"
    "$XPF_DIR/src/non_ppl.c"
    "$XPF_DIR/src/ppl.c"
    "$XPF_DIR/src/xpf.c"
)
choma_sources=("$CHOMA_DIR"/src/*.c)
[[ -e "${choma_sources[0]}" ]] || die "ChOma 源码不完整"
xpf_objects=()
xpf_index=0
for source in "${xpf_sources[@]}" "${choma_sources[@]}"; do
    object="$STATIC_DIR/obj/xpf/$xpf_index.o"
    xcrun --sdk iphoneos clang -c -O2 -fblocks -arch arm64e \
        -isysroot "$IOS_SDK" -miphoneos-version-min=16.5.1 \
        -I"$XPF_DIR/src" -I"$CHOMA_DIR/include" \
        "$source" -o "$object" \
        || die "XPF 静态对象编译失败：$source"
    xpf_objects+=("$object")
    xpf_index=$((xpf_index + 1))
done
xcrun --sdk iphoneos libtool -static -o "$STATIC_DIR/libxpf.a" "${xpf_objects[@]}" \
    || die "libxpf.a 归档失败"
XPF_ARCHIVE_SYMBOLS="$(LC_ALL=C xcrun nm -g "$STATIC_DIR/libxpf.a")"
grep -q ' _xpf_start_with_kernel_path$' <<<"$XPF_ARCHIVE_SYMBOLS" \
    || die "libxpf.a 缺少公开入口"
LC_ALL=C grep -a -q -- "arm_maxoffset" "$STATIC_DIR/libxpf.a" \
    || die "libxpf.a 缺少 arm_maxoffset 兼容 finder"

# libgrabkernel2 的 src/*.m 只包含 grab/appledb/utils；Partial 基类由固定提交
# 中的 _external/lib/ios/libpartial.a 提供。当前 target 的 Partial.m 仅实现
# kc_* 快路径，不能替代也不能重复定义 Objective-C Partial 类。
GRABKERNEL_COMMIT=e015c73aee6c2d3f6b0aad3fa629fe4c0429b7a6
GRAB_PARTIAL_SHA256=83aea6edd5d538bf72a91ec8feb4847eb2ae99612e56fd9aa61ee9dfccca3241
GRABKERNEL_DIR="$ROOT/build/deps/libgrabkernel2"
if [[ ! -d "$GRABKERNEL_DIR/.git" ]]; then
    [[ ! -e "$GRABKERNEL_DIR" ]] || die "libgrabkernel2 依赖目录存在但不是 Git checkout"
    mkdir -p "$(dirname "$GRABKERNEL_DIR")"
    git clone --no-checkout https://github.com/alfiecg24/libgrabkernel2.git \
        "$GRABKERNEL_DIR" >/dev/null 2>&1 || die "无法拉取 libgrabkernel2"
fi
git -C "$GRABKERNEL_DIR" fetch --depth 1 origin "$GRABKERNEL_COMMIT" >/dev/null 2>&1 \
    || die "无法获取 libgrabkernel2 固定提交 $GRABKERNEL_COMMIT"
git -C "$GRABKERNEL_DIR" checkout --detach "$GRABKERNEL_COMMIT" >/dev/null 2>&1 \
    || die "无法切换 libgrabkernel2 固定提交"
[[ "$(git -C "$GRABKERNEL_DIR" rev-parse HEAD)" == "$GRABKERNEL_COMMIT" ]] \
    || die "libgrabkernel2 版本不一致"
grab_sources=("$GRABKERNEL_DIR"/src/*.m)
[[ -e "${grab_sources[0]}" ]] || die "libgrabkernel2 源码不完整"
GRAB_PARTIAL_FAT_ARCHIVE="$GRABKERNEL_DIR/_external/lib/ios/libpartial.a"
[[ -f "$GRAB_PARTIAL_FAT_ARCHIVE" ]] || die "libgrabkernel2 缺少固定 Partial 静态库"
[[ "$(shasum -a 256 "$GRAB_PARTIAL_FAT_ARCHIVE" | awk '{print $1}')" == \
   "$GRAB_PARTIAL_SHA256" ]] || die "libgrabkernel2 Partial 静态库摘要不一致"
GRAB_PARTIAL_ARCHIVE="$STATIC_DIR/libpartial-arm64e.a"
xcrun lipo "$GRAB_PARTIAL_FAT_ARCHIVE" -thin arm64e \
    -output "$GRAB_PARTIAL_ARCHIVE" \
    || die "无法提取 libpartial arm64e slice"
GRAB_PARTIAL_SYMBOLS="$(LC_ALL=C xcrun nm -g "$GRAB_PARTIAL_ARCHIVE")"
GRAB_PARTIAL_CLASS_DEFINITIONS="$(awk \
    '$NF == "_OBJC_CLASS_$_Partial" && $(NF - 1) != "U" { count++ } END { print count + 0 }' \
    <<<"$GRAB_PARTIAL_SYMBOLS")"
[[ "$GRAB_PARTIAL_CLASS_DEFINITIONS" == 1 ]] \
    || die "libpartial arm64e slice 必须且只能定义一次 Partial 类"
grab_objects=()
grab_index=0
for source in "${grab_sources[@]}"; do
    object="$STATIC_DIR/obj/grabkernel/$grab_index.o"
    xcrun --sdk iphoneos clang -c -O3 -fPIC -fobjc-arc -arch arm64e \
        -isysroot "$IOS_SDK" -miphoneos-version-min=16.5.1 \
        -I"$GRABKERNEL_DIR/include" -I"$GRABKERNEL_DIR/_external/include" \
        "$source" -o "$object" \
        || die "libgrabkernel2 静态对象编译失败：$source"
    grab_objects+=("$object")
    grab_index=$((grab_index + 1))
done
xcrun --sdk iphoneos libtool -static -o "$STATIC_DIR/libgrabkernel2.a" \
    "${grab_objects[@]}" "$GRAB_PARTIAL_ARCHIVE" \
    || die "libgrabkernel2.a 归档失败"
GRAB_ARCHIVE_SYMBOLS="$(LC_ALL=C xcrun nm -g "$STATIC_DIR/libgrabkernel2.a")"
grep -q ' _grab_kernelcache$' <<<"$GRAB_ARCHIVE_SYMBOLS" \
    || die "libgrabkernel2.a 缺少 grab_kernelcache"
GRAB_ARCHIVE_PARTIAL_DEFINITIONS="$(awk \
    '$NF == "_OBJC_CLASS_$_Partial" && $(NF - 1) != "U" { count++ } END { print count + 0 }' \
    <<<"$GRAB_ARCHIVE_SYMBOLS")"
[[ "$GRAB_ARCHIVE_PARTIAL_DEFINITIONS" == 1 ]] \
    || die "libgrabkernel2.a 中 Partial 类定义数量不唯一"
ok "XPF 与 libgrabkernel2 静态库已就绪（arm64e / iOS 16.5.1）"


DERIVED="$ROOT/build/DerivedDataSJZ"
say "构建三角洲独立 App ($CONFIG)..."
xcodebuild -project "$ROOT/lara.xcodeproj" -scheme lara \
    -configuration "$CONFIG" -derivedDataPath "$DERIVED" \
    -destination 'generic/platform=iOS' CODE_SIGNING_ALLOWED=NO \
    CODE_SIGNING_REQUIRED=NO CODE_SIGN_IDENTITY="" build \
    2>&1 | tee "$ROOT/build/xcodebuild-sjz.log"
APP="$DERIVED/Build/Products/$CONFIG-iphoneos/SJZ Overlay.app"
[[ -f "$APP/SJZ Overlay" ]] || die "未找到构建结果"
[[ -f "$APP/SJZChinese.otf" ]] || die "缺少中文字体资源"
ok "App: $APP"
[[ "$PACKAGE" == 1 ]] || exit 0
command -v codesign >/dev/null || die "缺少 codesign"
command -v zip >/dev/null || die "缺少 zip"
codesign --force --sign - --timestamp=none --entitlements "$ROOT/Config/lara.entitlements" "$APP"
STAMP="$(date +%Y%m%d-%H%M%S)"
STAGE="$(mktemp -d "$ROOT/build/sjz-package.XXXXXX")"
trap 'rm -rf -- "$STAGE"' EXIT
mkdir "$STAGE/Payload"
cp -R "$APP" "$STAGE/Payload/"
OUTPUT="$ROOT/build/SJZ-Overlay-$STAMP.ipa"
[[ ! -e "$OUTPUT" ]] || die "输出已存在"
(cd "$STAGE" && zip -qry "$OUTPUT" Payload)
shasum -a 256 "$OUTPUT" > "$OUTPUT.sha256"
ok "IPA: $OUTPUT（ad-hoc 签名，安装权限需由设备环境满足）"
