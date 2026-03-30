# 反引号宽度测试

反引号（代码标记）周围的空白应该与正常文本间距一致。

## 内联代码示例

调用 `getValue()` 函数返回值。

使用 `setProperty()` 设置属性值。

在 `onInit()` 回调中初始化。

常见的 `m_player` 变量。

返回 `nullptr` 作为默认值。

## 对比测试

这是普通文本中间有 `getValue()` 代码。

函数名 `processUserData()` 在句子中。

变量 `userDataExtended` 和 `m_player` 应该等宽。

## 验证项

- 反引号宽度占比应该 <= 20%
- 在 A、B 屏表现一致
- 不应该看到明显的多余空白
