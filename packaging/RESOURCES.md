# railhmi 资源与图标来源/许可登记（ITEM-025）

本应用 UI 颜色/字号全部来自构建期生成的 `Theme`（`docs/hmi/design_tokens.yaml` → `tokens.json`/`Theme.qml`），
不引入第三方图标包；状态/级别字形为 Unicode 实心字形（●/↻/▲/■/⊖/◆），无外部素材版权问题。

| 资源 | 类型 | 来源 | 许可 | 备注 |
|---|---|---|---|---|
| 设计令牌/颜色/字体层级 | 自绘 | 本项目 `docs/hmi/design_tokens.yaml` | 项目自有 | 生成 Theme/Tokens.qml |
| 状态/报警字形（●↻▲■⊖◆） | Unicode 字符 | Unicode 标准字符集 | 无需授权 | 字体由系统提供 |
| UI 字体 | 系统字体栈 | Noto Sans CJK / Source Han Sans / WenQuanYi / Noto Sans（系统安装） | 各自开源许可（OFL/Apache 等） | 应用只引用字体族名，不打包字体 |
| 模型 `yolov8s-seg.rknn`（rail） | 模型 | 项目提供（历史训练/转换） | 项目自有 | 由模型职责提供，应用只消费版本/结果 |
| 模型 `yolov5s.rknn`（person, COCO） | 模型 | 项目提供 | 上游 COCO 权重许可需模型职责确认 | 仅人形检测基线 |

说明：若后续引入开源图标集（如 Lucide ISC / Material Symbols Apache-2.0），须在本表逐项登记来源与许可。
当前实现未引入任何未授权素材。
