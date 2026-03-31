# 代码块渲染测试

## 围栏代码块（C++）

```cpp
#include <iostream>
#include <vector>

int main() {
    std::vector<int> nums = {1, 2, 3, 4, 5};
    for (auto n : nums) {
        std::cout << n << " ";
    }
    return 0;
}
```

## 围栏代码块（Python）

```python
def fibonacci(n):
    if n <= 1:
        return n
    return fibonacci(n-1) + fibonacci(n-2)

for i in range(10):
    print(fibonacci(i), end=" ")
```

## 无语言标记的代码块

```
这是一个没有语言标记的代码块
第二行
第三行
```

## 行内代码

这是 `inline code` 混合在普通文本中。多个行内代码：`a`、`b`、`c`。

长行内代码：`std::unordered_map<std::string, std::vector<int>>` 不应溢出。

## 代码块后的段落

这个段落在代码块后面，不应有额外空白或重叠。
