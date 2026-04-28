# 代码块软换行回归样本

> 防御：代码块中超长行不自动换行，导致整行越界、被裁剪或压住后续段落。
> 验收要点：「视口边界」检查中代码块 `x + width <= viewport_w + 5`；
> 「代码块空白」检查中高度未严重超过 `行数 × 行高` 的合理上界。

## 短行（应保留语法高亮）

```python
def hello():
    print("hi")
```

## 超长行（应字符级软换行）

```bash
echo "this is a very very very very very very very very very very very very very very very very long line that must exceed any reasonable preview width"
curl -X POST https://api.example.com/v1/some/very/long/endpoint/path/that/never/ends/and/keeps/going/and/going/and/going?query=value&another=parameter
```

## 中文混合超长行（断行不应割裂双字节字符）

```text
这是一段非常非常非常非常非常非常非常非常非常非常非常非常非常非常非常非常非常非常非常非常长的中文测试内容，用于验证 UTF-8 字符在软换行边界上的正确处理。
```

## 末尾段落（验证不被覆盖）

如果代码块高度计算正确，这一段不会被压在代码块下方。
