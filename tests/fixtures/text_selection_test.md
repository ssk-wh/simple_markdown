# 文本选中测试

## 测试用例 1

代码块中的变量选中测试：

```cpp
void example() {
    int m_player = 0;
    const auto extendMap = map.value("extend").toMap();
    QWidget* bgWidget = new QWidget(this);
    bgWidget->show();
}
```

双击 `m_player` - 应该选中整个单词，不包括前面的符号。

## 测试用例 2

带符号的标识符：

```cpp
void initPlayer() {
    if (m_player)
        return;

    const auto& PlayerThumbnail = getThumbnail();
    const auto extendMap = map.value("extend").toMap();
}
```

双击 `PlayerThumbnail` - 应该只选中 PlayerThumbnail，不包括前面的 `&`。

## 测试用例 3

长变量名：

```cpp
void processUserDataExtended() {
    QMap<QString, QVariant> userDataExtended;
    userDataExtended.insert("name", "test");

    ProcessorFactory processorFactory;
    processorFactory.initialize();
}
```

双击任何位置 - 应该准确选中对应的单词。
