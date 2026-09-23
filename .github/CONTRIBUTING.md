# 贡献与维护政策

Deskflow FileCopy 是基于 [Deskflow](https://github.com/deskflow/deskflow) 的独立非官方衍生项目。功能范围和构建步骤见[项目 README](../README.md)。

## 仓库写入与发布

本仓库由 [@EugeneEvan](https://github.com/EugeneEvan) 单独维护。只有仓库主人直接推送、合并和发布，不向普通使用者授予协作者写权限，不自动合并外部 Pull Request。

公开仓库允许公众查看、克隆和拉取代码。欢迎按许可证 fork、修改、再分发，或者通过 Issue / Pull Request 提供修复建议。是否将外部修改合入本仓库，由仓库主人审阅决定；提交建议不等于获得本仓库写权限。

`CODEOWNERS` 用于声明审核归属，不会自行授予或撤销写权限；实际写权限和合并要求由 GitHub 仓库设置控制。该说明遵循 [GitHub 的 CODEOWNERS 定义](https://docs.github.com/en/repositories/managing-your-repositorys-settings-and-features/customizing-your-repository/about-code-owners)。

## 报告问题

请先确认使用的是本项目版本，并在本仓库提交问题。建议说明：

- 两端 Windows 版本、应用版本、服务端/客户端角色和运行模式。
- TLS、剪贴板共享、文件复制是否已开启。
- 能够复现的操作顺序、预期结果、实际结果及必要的脱敏日志。
- 文件数量、总大小、是否包含目录、中文路径或特殊文件类型；不需要上传原始业务文件。

不要在公开 Issue 或 Pull Request 中上传密码、Token、Cookie、证书私钥、完整环境配置和业务数据。未确认属于上游的问题，不要直接提交给 Deskflow 上游。

## 提交修改建议

1. 在自己的 fork 或本地分支开发，控制修改范围并保留现有版权、SPDX 和许可声明。
2. 说明触发问题、修改后的行为、兼容性影响，以及真实执行过的验证；未执行的双机测试应明确列出。
3. 按 README 运行相关构建和测试，C++ 格式使用 `clang-format 20.1.0`。
4. 提交简洁、明确的中文提交信息，技术标识保留原文，例如 `fix: 修复取消文件传输后的缓存清理`。
5. 通过 Pull Request 提交，由仓库主人审核。测试日志、临时文件、截图和含机器路径的排查记录无需提交到源码仓库，可在说明中提供脱敏摘要。

## 许可证与上游归属

仓库维护政策不改变许可证赋予公众的权利。主要应用代码沿用 GPL-2.0-only 及对应 OpenSSL 例外；各文件、图标、构建脚本和第三方内容按其自身许可处理。请保留 [LICENSE](../LICENSE)、[LICENSES](../LICENSES)、[REUSE.toml](../REUSE.toml) 和上游版权声明，不把上游实现声明为独立原创。
