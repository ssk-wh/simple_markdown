---
type: bug
status: draft
priority: P1
created: 2026-06-24
related_specs:
  - specs/模块-app/23-检查更新.md
---

# 检查更新遇到 GitHub API 限流

## 背景
自动检查更新时调用 GitHub Releases API，未认证请求有 60 次/小时 的限流，
频繁触发可能导致检测失败（HTTP 403/429）。

## 目标
- [ ] 避免因 API 限流导致更新检查不可用
- [ ] 用户侧不因限流弹出错误或崩溃

## 设计
候选方案：
1. 缓存上次检查结果，一定时间内不重复请求
2. 缓存 ETag/If-None-Match 做条件请求
3. 兜底：API 失败时静默忽略，不弹错误

## 任务
- [ ] 调研当前 UpdateChecker 实现
- [ ] 确定限流缓解方案
- [ ] 实现 + 测试
- [ ] 回归测试

## 进展
- 2026-06-24 创建
