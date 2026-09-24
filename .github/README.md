# Deskflow FileCopy

这是基于 [Deskflow](https://github.com/deskflow/deskflow) 的非官方二次开发项目，在原有键鼠共享功能上增加 Windows ↔ Windows 文件复制。

**功能、使用步骤、限制、构建与维护政策，请阅读[仓库根目录 README](../README.md)。**

- 文件复制需要两端安装本项目同版程序、启用 TLS 和文件复制，并在服务端启用剪贴板共享。
- 复制后先接收到缓存，接收端提示就绪后再到目标文件夹按 Ctrl+V。
- 0.2.0 增加缓存目录与配额管理、速度/预计剩余时间和文件数量；安装时可选择当前用户登录后启动，默认关闭。
- 下载以[本仓库 Releases](https://github.com/EugeneEvan/deskflow-filecopy/releases) 的实际附件为准；上游 Deskflow 的发布包不包含这个扩展。
- 本扩展问题在本仓库反馈；[贡献与维护政策](CONTRIBUTING.md)说明了仓库主人的审核和发布职责。

本项目不由 Deskflow 上游团队发布或背书。需要原版 Deskflow 时可访问[上游主页](https://github.com/deskflow/deskflow)、[上游发布页](https://github.com/deskflow/deskflow/releases)和[上游构建文档](https://github.com/deskflow/deskflow/wiki/Building)。

保留上游贡献者归属与原有许可；详见 [LICENSE](../LICENSE)、[LICENSES](../LICENSES) 和 [REUSE.toml](../REUSE.toml)。
