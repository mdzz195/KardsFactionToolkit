# KardsFactionToolkit

简体中文 | [English](README.en.md)

为 KARDS Windows x64 制作新增国家 DLL 的源码工具。默认配置为 **罗马尼亚盟国：`Romania = 13`，牌组编码 `b`**。修改 JSON 后重新构建，可把多个新增盟国编入同一个 `dwmapi.dll`。

这是从已运行的 `RomaniaAllyNative 0.1.1` 整理、参数化得到的独立 C++ 项目。无需安装 Unreal Engine 或 UE4SS，运行时不使用 Lua。DLL 在游戏启动时扩展国家枚举和三处原生国家转换；国家名称、图标、卡牌和界面仍通过 UE 资源制作。

## 能做什么

- 保留游戏现有国家数值 `0..12`，从 `13` 开始追加国家。
- 新增国家名 → 枚举值、枚举值 → 牌组字符、牌组字符 → 枚举值的转换。
- 用 `config/factions.json` 定义国家，检查重名、重复编码、非法编号。
- 用版本配置检查 EXE 的 PE 字段、关键函数机器码和原枚举结构，支持文件名为 `1.exe` 的同版本 EXE；**不要求整文件 SHA256 一致**。
- 自动发现 Visual Studio、生成转发代码、编译、运行隔离测试并输出 DLL。
- 生成适合上传 GitHub 的纯源码 ZIP。

当前版本的目标是**新增盟国**。主国的总部、卡背、进度和默认卡组等分支需要另外实现。DLL 不会自动创建国家按钮，也不修改服务端规则；服务器是否接受新国家和新牌组编码，需要在具体环境验证。

## 快速构建

需要 Windows x64，以及：

1. Visual Studio 2022 或 2026，安装“使用 C++ 的桌面开发”、MSVC x64、Windows SDK 和“适用于 Windows 的 C++ CMake 工具”。
2. Python 3.10+ **64 位**。
3. 本机与版本配置匹配的 KARDS EXE。项目不附带游戏文件。

在项目根目录打开 PowerShell：

```powershell
python -m pip install -r requirements.txt

.\Build.ps1 -GameExe 'D:\kards\KARDS\default\game\kards\Binaries\Win64\1.exe'
```

示例路径按实际安装位置修改；代码没有固定磁盘路径。也可以传入 `kards-Win64-Shipping.exe`。

成功后得到：

```text
dist/
  dwmapi.dll             最终 DLL
  manifest.json          国家配置、版本标识、构建样本哈希
  SHA256SUMS.txt          DLL 校验值
  README.md
  README.en.md           英文说明
  LICENSE
  THIRD_PARTY_NOTICES.md
  licenses/MinHook.LICENSE.txt
```

构建脚本只写项目输出目录，**不会部署到游戏**。测试失败时会停止，不会把本次 DLL 安装到 `dist`；该目录中之前成功构建的旧文件可能仍在，请以本次命令成功和哈希为准。

如果 PowerShell 的本机策略阻止脚本执行，可仅对本次进程执行：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\Build.ps1 -GameExe 'D:\你的游戏目录\1.exe'
```

## 配置新国家

默认 `config/factions.json`：

```json
{
  "schema_version": 1,
  "factions": [
    {"name": "Romania", "display_name": "罗马尼亚", "value": 13, "deck_code": "b"}
  ]
}
```

| 字段 | 含义和要求 |
| --- | --- |
| `name` | 内部国家名。使用 ASCII 英文字母开头，可包含数字和下划线，最长 48 字符。运行时国家名匹配忽略大小写。 |
| `display_name` | 文档、构建清单中的显示名，可写中文。**不会自动写入游戏名称表或本地化。** |
| `value` | 原生枚举数值，必须按配置顺序从 13 连续增加，不改已有 `Allies=11`、`Neutral=12`。 |
| `deck_code` | 单个小写字符 `b..z`，同一配置中不可重复。`0..9`、`a` 留给原版。 |

例如再加一个盟国：

```json
{
  "schema_version": 1,
  "factions": [
    {"name": "Romania", "display_name": "罗马尼亚", "value": 13, "deck_code": "b"},
    {"name": "Hungary", "display_name": "匈牙利", "value": 14, "deck_code": "c"}
  ]
}
```

所有新增国家应放入**同一个 DLL**，不是每个国家装一个同名 DLL。配置最多容纳 25 个新增条目；完整游戏界面支持并未对这个上限逐一验证。已经用于资源或牌组的编号与字符应保持稳定，改动它们会改变原有数据的含义。

上述双国家示例已保存为 `config/example.two-allies.json`，可通过 `-FactionConfig` 传入。默认文件仍只有罗马尼亚。

也可使用单独配置文件：

```powershell
.\Build.ps1 -GameExe 'D:\你的游戏目录\1.exe' -FactionConfig '.\config\factions.json'
```

## 安装、日志与卸载

1. 退出游戏。
2. 将 `dist/dwmapi.dll` 放在**实际启动的游戏 EXE 同目录**，通常是 `kards/Binaries/Win64`。
3. 如果该目录已有其他工具的 `dwmapi.dll`，先分清其来源并备份。此项目没有实现同名代理串联；两个文件不能占用同一路径。不要将文件复制到 Windows 系统目录。
4. 按平时方式启动游戏。同目录生成 `KardsFactionToolkit.log`。

默认配置安装成功时会包含：

```text
READY: EFactionEnum::Romania=13, deck code=b; original faction values 0..12 preserved
READY: early enum registration and all three native deck conversions installed
```

文件名保持 `dwmapi.dll`，因为本版本游戏从该名称导入系统 DWM API。DLL 将系统导出转发到 Windows `System32` 的原库；随意改名后不会自动加载。

`READY` 表示原生扩展已安装，不表示 UI、卡表、牌组保存或服务器已验证通过。版本不匹配时，DLL 不启用国家扩展；在入口校验阶段就拒绝时可能没有日志，仍保持 DWM 转发。

卸载时退出游戏，再移走本工具的 `dwmapi.dll`，恢复之前的同名文件（如有）。不要在游戏运行中卸载模块，UE 的枚举名称和函数跳转仍引用其中的内存。

## UE 端还要做什么

DLL 只处理 EXE 的国家识别。完整国家还需要配置：

- 编辑器 `EFactionEnum` 与 DLL 中相同的内部名称和数值。
- 国家名称表 `DT_FactionNames`、图标表 `DT_FactionIconTextures`，以及实际引用的声音/外观表。
- `FactionFunctions`、`UtilityFunctions`、`DeckAndCollectionUtilityFunctions` 中涉及国家范围、盟国列表和默认分支的逻辑。
- 新建卡组的 `W_CreateDeck` 与 `selectFactionButton_Widget` 实例。
- 收藏/组卡筛选中的 `W_CollectionFilters`、`W_DeckBuilder_FilterWidget`：只改其中一个入口不保证两处都出现图标。
- 卡牌国家、资产注册表和实际打包资源。

详见 [UE 配套资源说明](docs/UE_INTEGRATION.md)。这些表格、蓝图、图标和已修改的游戏资源不包含在本源码项目里。

## 支持的游戏版本

内置配置：[`profiles/kards-20261002-win64.json`](profiles/kards-20261002-win64.json)。

| PE 字段 | 验证值 |
| --- | --- |
| Machine | AMD64 / `0x8664` |
| TimeDateStamp | `0xfe01dfb1` |
| SizeOfImage | `0x9cc4000` |
| AddressOfEntryPoint | `0x6ea1e10` |

“使用 UE 5.6”本身并不代表二进制布局相同。游戏更新后不要只改时间戳或跳过校验；需要重新确认函数 ABI、枚举结构及位置。维护方法见 [版本适配与实现](docs/DEVELOPMENT.md)。

## 测试

默认构建会运行：

- Python 配置、版本指纹和源码打包测试。
- 隔离映射本地游戏 EXE，执行原版三处国家转换函数，检查原国家回归、新国家往返、重复调用、FString 释放、失败拒绝及安装回退。**不执行游戏启动入口，不连接服务器。**
- 独立测试进程通过导入表加载生成的 `dwmapi.dll`，解析当前系统导出并调用游戏使用的四个 DWM API。

同时验证另一个同版本 EXE：

```powershell
.\Build.ps1 -GameExe 'D:\你的游戏目录\1.exe' `
  -AdditionalTestExe 'D:\你的游戏目录\kards-Win64-Shipping.exe'
```

独立运行不依赖游戏文件的测试：

```powershell
python -m unittest discover -s tests -p 'test_*.py' -v
```

GitHub Actions 仅运行上述 Python 测试、依赖完整性检查和源码打包；不会下载游戏或声称已经验证对局。

本地构建与测试结果见 [0.2.0 验证记录](docs/VALIDATION.md)。

## 上传 GitHub

建议上传整个源码目录，或生成干净源码包：

```powershell
python tools/package_source.py
```

得到 `dist/KardsFactionToolkit-source.zip`。它采用文件白名单，不包含 EXE、DLL、游戏资源、日志、生成的机器码头文件、个人路径或构建缓存。解压后将源码文件提交到仓库。`dist/dwmapi.dll` 如需发布，可单独作为 Release 附件，并附上 `dist/licenses`、许可证和清单。

构建和打包脚本不会自动连接远程仓库或发布内容。`.gitignore` 已排除本机产物。

## 源码布局

```text
config/                 新国家配置
profiles/               支持的游戏版本地址与指纹
src/core.cpp            枚举扩展、国家字符串/牌组编码 Hook
src/proxy.cpp           启动期安装、系统 DWM 转发、状态接口
tools/generate_profile.py  配置与版本验证、生成 C++/MASM
tools/package_source.py    生成纯源码 ZIP
tests/                  Python、原生转换和 DWM 代理测试
vendor/minhook/         MinHook 1.3.4 源码及原许可证
docs/                   UE 配套说明、实现及版本维护说明
```

项目代码采用 [MIT License](LICENSE)。第三方组件保留各自许可证，详见 [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)。
