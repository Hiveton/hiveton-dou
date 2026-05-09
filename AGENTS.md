# AGENTS.md

## 工程约定

- 当前工程 UI 设计尺寸为 `528 x 792`，坐标按固定设计像素使用。
- `app/src/ui/ui_helpers.c` 中的 `ui_px_x()`、`ui_px_y()`、`ui_px_w()`、`ui_px_h()` 当前直接返回传入值，不做比例缩放。
- 墨水屏页面应优先按真实可视区域排布，避免依赖滚动补救布局问题。
- 新增页面时优先复用现有 `ui_helpers`、`ui_i18n`、`ui_runtime_adapter` 等工程内工具，不另起一套 UI 框架。

## UI 页面开发

- 使用 `ui_build_standard_screen()` 创建的标准页面，内容放在 `page.content` 内，用固定坐标布局。
- 不要在同一个页面混用多套布局系统。
- 顶部导航和底部 `tabbar` 是全局复用组件。实现单个业务页面时，不允许修改它们的视觉样式、尺寸、坐标、选中态切图或整体布局，除非任务明确要求重构全局导航组件本身。
- 业务页面必须调用共享的顶部导航和底部 `tabbar` 组件，只能传入当前页面的选中状态并接入页面切换事件；不要在页面源码中复制、重画或 fork 一套导航/`tabbar`。
- 新增 UI 页面时，需要同步接入以下位置：
  - `app/src/ui/ui_types.h`：增加 `UI_SCREEN_*` 枚举
  - `app/src/ui/ui.h`：声明页面全局对象和 init/destroy 函数
  - `app/src/ui/ui_runtime_adapter.c`：注册运行时页面
  - `app/src/ui/ui.c`：加入销毁顺序或页面轮转序列
  - `app/src/ui/filelist.txt`：加入新页面 `.c` 文件
  - `app/src/ui/CMakeLists.txt`：加入新页面 `.c` 文件
- 如果页面需要硬件按键 `T/B`，还要在 `app/src/ui/ui_runtime_adapter.c` 的硬件按键分发逻辑中接入对应处理函数。

## 配置与存储

- TF 卡挂载后会创建常用目录，包括 `record`、`books`、`mp3`、`pic`、`font`、`config`。
- 运行时配置通常放在可发现的 `config` 目录下，常见路径包括：
  - `/config`
  - `/tf/config`
  - `/sd/config`
  - `/sd0/config`
  - `config`
- 字体配置目录发现逻辑可参考 `app/src/ui/ui_font_manager.c`。
- 新增持久化配置时，应优先沿用已有配置目录发现方式，避免只写死单一路径。

## EPUB 阅读

- EPUB 解析入口在 `app/src/reading/reading_epub.c`，公开接口在 `app/src/reading/reading_epub.h`。
- 当前 EPUB 模块已支持：
  - 读取 `META-INF/container.xml`
  - 定位 OPF 文件
  - 解析 `manifest`
  - 解析 `spine`
  - 加载章节内容
  - 解码 EPUB 内部图片
- EPUB 封面通常需要从 OPF 中识别：
  - EPUB3：`properties="cover-image"`
  - EPUB2：`<meta name="cover" content="...">`
  - 旧格式：`<reference type="cover" href="...">`
- 获取封面后，可复用 `reading_epub_decode_image()` 解码内部图片路径。

## 编译下载

- 目标板卡：`sf32lb52-lcd_n16r8`
- 从仓库根目录执行时，流程为：

```bash
cd sdk
source export.sh
cd ../app/project
scons --board=sf32lb52-lcd_n16r8
cd build_sf32lb52-lcd_n16r8_hcpu
./uart_download.sh
```

- `uart_download.sh` 提示选择串口时，当前常用选择为 `0`。
- 编译失败时不要继续下载，先回报 `scons` 的错误信息。

## 常用命令

- 查找 UI 页面文件：

```bash
rg --files app/src/ui/screens
```

- 查找页面枚举和路由引用：

```bash
rg "UI_SCREEN_" app/src/ui
```

- 查看标准页面布局辅助函数：

```bash
sed -n '2827,2955p' app/src/ui/ui_helpers.c
```

- 查看字体配置目录发现逻辑：

```bash
sed -n '20,120p' app/src/ui/ui_font_manager.c
```

- 查找 EPUB 解析相关代码：

```bash
rg -n "epub|opf|manifest|spine|cover" app/src/reading app/src/ui/screens
```

## 协作注意

- 不要回退用户已有改动。
- 不要使用破坏性 git 命令，除非用户明确要求。
- 修改 UI 时优先考虑设备实际显示效果，避免内容超出 `528 x 792` 可视区域。
- 墨水屏项目默认按黑白 UI 处理，不引入依赖颜色区分状态的交互。
