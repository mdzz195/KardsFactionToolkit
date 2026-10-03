# 实现与维护

## 构建

推荐使用根目录 `Build.ps1`。也可直接用 CMake：

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 `
  -DKARDS_GAME_EXE="D:/你的游戏目录/1.exe"
cmake --build build --config Release --parallel 2
ctest --test-dir build -C Release --output-on-failure
cmake --install build --config Release --prefix dist
```

Visual Studio 2026 使用 `Visual Studio 18 2026` 生成器及支持它的 CMake。更换生成器时使用新的构建目录，例如 `-BuildDir build-vs2026`。Python 不在 PATH 时传 `-Python`，CMake 不在 PATH 时传 `-CMake`。

生成的 `profile.h`、`factions.h`、MASM、DEF 和构建清单位于 `build/generated`，不会写回 `src`。输入 EXE、配置、版本配置或生成器源码变化都会触发重新生成。Release 静态链接 MSVC 运行库。

`Build.ps1 -SkipTests` 显式跳过测试，仅用于开发迭代。默认不要跳过。不同国家配置的产物需要隔离时，同时传入不同的 `-BuildDir` 和 `-OutputDir`。

## 启动与内存生命周期

1. 游戏通过 `dwmapi.dll` 导入加载模块。
2. `DllMain` 仅做有界指纹核对和入口跳转设置；不在加载器锁内初始化 MinHook。
3. 到达 EXE 入口时先恢复原入口机器码，再固定 DLL 的生命周期并安装国家扩展。
4. 验证原 `FEnumParams` 和 13 个枚举条目；构造“原条目 + 配置中的新条目”。
5. 安装名称转枚举、枚举转牌组字符、牌组字符转枚举三个 Hook。
6. 进入原始 EXE 启动流程，UE 枚举注册使用扩展后的条目。

未知国家和原有国家交回原函数。原生解析函数拥有传入的 `FString`，即使匹配新增国家也继续让原函数释放输入。编码复用原版 ANZAC 的单字符分配结果，仅改字符，避免跨分配器管理内存。

安装失败时撤回已有 Hook 和枚举参数写入；枚举注册后不能卸载 DLL，因为名称数组和 detour 仍被引用。`RemoveBeforeEnumConstruction` 仅供注册前回退和隔离测试使用。

## 诊断导出

`KardsFactionStatus()`：

| 返回值 | 含义 |
| --- | --- |
| `0` | 当前进程未匹配，未设置启动跳转 |
| `1` | 启动跳转已设置 |
| `2` | 国家扩展安装成功 |
| `-3` | 固定模块生命周期失败 |
| `-4` | 国家扩展校验/安装失败 |

`KardsFactionVersion()` 返回工具版本。兼容保留 `RomaniaNativeStatus` / `RomaniaNativeVersion` 别名；这些是查询接口，不是运行时动态添加国家的接口。

系统 DWM 转发列表来自构建机器的 x64 `System32/dwmapi.dll`，名称和 ordinal 均保留。对外分发时应在目标 Windows 版本检查游戏实际使用的 DWM API；并非每个 Windows 版本都拥有相同的私有导出。

链接时可能出现两个 `LNK4222` 警告，涉及 `DllCanUnloadNow` / `DllGetClassObject`。这是链接器对 COM 导出分配序号的提示；本代理刻意保留系统原有序号，不能为了消除提示改动序号。系统导出转发由代理测试验证。

## 游戏更新后的适配

`profiles/*.json` 是针对特定游戏原生布局的配置，不是自动寻址器。推荐用 IDA 或同等反汇编工具确认：

1. PE 时间戳、映像大小与入口，以及入口可恢复的完整指令序列。
2. 国家名解析、编码、解码、`EFactionEnum` 构造函数的 ABI 与函数边界；用异常目录的边界核对完整函数 SHA256。
3. `FEnumParams` 结构布局、原条目数量与顺序、`Outer`、名称地址和两个 singleton 地址。
4. 隔离测试使用的 FString 释放函数和构造函数地址。当前 ABI 是 Windows x64，FString 为 16 字节、枚举条目 16 字节、参数结构 48 字节。
5. 原版编码确实仍以 ANZAC 生成 `a`，原有国家的字符和保留值未改变。

只有地址变化且 ABI/语义未变时，才可能仅新增 JSON profile；原枚举项或结构变化时必须修改 `src/core.cpp` 和测试。生成器拒绝错误的版本或函数哈希，不提供强行通过开关。

整文件 SHA256 记录到本地 `manifest.json` 用于追溯，**不作为运行时允许列表**。改名为 `1.exe` 或修改无关部分不会单独导致拒绝；关键函数、PE 字段或枚举参数变化会拒绝。

## 测试与发布

原生测试只映射用户提供的本地 EXE，在隔离内存中重定位并执行指定函数；替换两个分配辅助函数用于跟踪释放，不启动游戏。负向测试会主动打印指纹或晚安装错误，末尾 PASS 才表示测试完成。

GitHub CI 没有游戏样本，只运行 Python 验证与源码包完整性检查。手工发布 DLL 前，在支持的本地 EXE 上运行完整测试；游戏 UI、牌组、服务器和对局应另行验证。

源码包采用白名单。第三方 MinHook 文件由 `vendor/minhook/upstream.json` 记录来源与 SHA256，CI 核对这些原始文件。不得把本地 `build/generated/profile.h`、游戏 EXE、解包资源或个人诊断日志补进源码包。
