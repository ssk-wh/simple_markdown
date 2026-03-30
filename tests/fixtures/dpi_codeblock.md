# DPI 代码块测试

这是高 DPI 下代码块的测试。代码块下方不应该有多余的空白。

## 代码块 1

```cpp
void initPlayer() {
    if (m_player)
        return;

    const auto extendMap = map.value("extend").toMap();
    m_player->setPlayerDocCtxId(ctxId);
    m_player->triggerAction("initPage");
}
```

## 代码块 2

```python
def calculate_metrics(font_size, dpi_ratio):
    base_height = font_size * 1.5
    actual_height = base_height * dpi_ratio
    return actual_height

result = calculate_metrics(14, 1.5)
```

## 代码块 3

```javascript
function findCodeblocks(image) {
    const blocks = [];
    for (let y = 0; y < image.height; y++) {
        for (let x = 0; x < image.width; x++) {
            // 识别代码块背景色
        }
    }
    return blocks;
}
```

这段文本在代码块后面，用来检查代码块下方是否有多余空白。
如果有额外的空行，说明高度计算有误。
