# 应用商店发布目录

`catalog.json` 由 `tools/package_store.py --tag <release>` 生成，指向公开 GitHub Releases 的独立应用包。应先构建、验证并上传 Release 附件，再把生成的目录复制到这里提交。不要先发布指向不存在附件的目录。

基础安装仍需通过 `tools/deploy.py` 完成，包括新版 launcher、应用商店、字体与公共工具。商店不会自动下载目录中的全部应用，也不会自动更新隐藏或未安装的应用。Launcher 与公共运行时不在可选应用目录内。
