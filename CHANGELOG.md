# Changelog

## 0.2.0 — 2026-10-03

- 从 RomaniaAllyNative 0.1.1 提取独立源码项目，保留原生枚举扩展与三处转换机制。
- 国家参数改为 JSON，支持在同一个 DLL 中追加多个盟国，默认 Romania=13 / b。
- 新增严格配置验证、独立游戏版本 profile、CMake 项目及自动发现 Visual Studio 的构建脚本。
- 生成文件写入构建目录，去除源代码中的本机绝对路径。
- 原生测试按配置验证全部新国家，保留原国家回归和 FString 生命周期测试。
- 增加 Python 测试、GitHub CI、纯源码打包、中文 README、UE 配套说明和第三方许可证。

## 来源：RomaniaAllyNative 0.1.1

原工作区版本使用固定罗马尼亚配置；已将整 EXE 哈希门槛改为 PE 版本字段、关键代码和枚举结构校验，支持相同版本的改名/无关内容差异。
