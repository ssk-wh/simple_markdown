// tests/viewer/ScrollModelTest.cpp
//
// Spec: specs/模块-viewer/02-滚动模型.md
// 验收：T1 ~ T14
#include <gtest/gtest.h>

#include <chrono>
#include <cmath>
#include <random>
#include <vector>

#include "ScrollModel.h"

using viewer::ScrollModel;

namespace {

// 假测量器：每个 chunk 有固定「真实高度」，记录被测量的次数
struct Fake {
    std::vector<double> real;
    std::vector<int> calls;
    explicit Fake(size_t n, double base = 100.0, double variance = 0.0, unsigned seed = 1)
        : real(n, base), calls(n, 0)
    {
        std::mt19937 g(seed);
        std::uniform_real_distribution<double> d(-variance, variance);
        for (auto& h : real) h = base + d(g);
    }
    ScrollModel::Measurer fn()
    {
        return [this](size_t i) { ++calls[i]; return real[i]; };
    }
    int totalCalls() const { int s = 0; for (int c : calls) s += c; return s; }
};

ScrollModel makeModel(Fake& f, uint32_t linesPerChunk = 4, double estPxPerLine = 25.0, double vp = 600.0)
{
    ScrollModel m;
    m.setInitialPxPerLine(estPxPerLine);
    m.setViewportHeight(vp);
    m.setMeasurer(f.fn());
    for (size_t i = 0; i < f.real.size(); ++i) m.appendChunk(linesPerChunk);
    return m;
}

}  // namespace

TEST(ScrollModelTest, T1_Empty)
{
    ScrollModel m;
    EXPECT_EQ(m.count(), 0u);
    EXPECT_DOUBLE_EQ(m.total(), 0.0);
    EXPECT_TRUE(m.visible().empty());
    m.scrollBy(100);
    m.scrollToEnd();
    m.setTopY(50);
    EXPECT_DOUBLE_EQ(m.topY(), 0.0);
}

TEST(ScrollModelTest, T2_EstimatedTotalIsSumOfLines)
{
    Fake f(10);
    auto m = makeModel(f, 4, 25.0);
    EXPECT_DOUBLE_EQ(m.total(), 10 * 4 * 25.0);
    EXPECT_DOUBLE_EQ(m.chunkTop(3), 3 * 100.0);
    EXPECT_EQ(m.indexAtY(0), 0u);
    EXPECT_EQ(m.indexAtY(99.9), 0u);
    EXPECT_EQ(m.indexAtY(100.0), 1u);
    EXPECT_EQ(m.indexAtY(1e9), 9u);
}

TEST(ScrollModelTest, T3_VisibleMeasuresOnlyViewportChunks)
{
    Fake f(1000);
    auto m = makeModel(f);
    auto v = m.visible();
    ASSERT_FALSE(v.empty());
    EXPECT_DOUBLE_EQ(v.front().screenY, 0.0);
    // 视口 600、每块 100 → 6 块被测量
    EXPECT_EQ(f.totalCalls(), 6);
    for (const auto& c : v) EXPECT_TRUE(m.isMeasured(c.index));
}

TEST(ScrollModelTest, T4_AnchorScreenYStableWhenAboveChunksMeasured)
{
    // 实高是估高的 2 倍：锚点在 500，之上的 chunk 随后被测量，锚点屏幕位置不得变化
    Fake f(2000, 200.0);
    auto m = makeModel(f, 4, 25.0);  // 估 100
    m.jumpTo(500, 30.0);
    auto before = m.visible();
    ASSERT_FALSE(before.empty());
    EXPECT_EQ(before.front().index, 500u);
    EXPECT_DOUBLE_EQ(before.front().screenY, -30.0);
    for (size_t i = 400; i < 500; ++i) m.invalidate(i);   // 无影响
    // 直接用 scrollBy 向上走一小段，触发上方 chunk 测量
    m.scrollBy(-250.0);
    auto after = m.visible();
    // 向上滚 250px 后，原锚点 chunk 的屏幕 y 应为 -30 + 250 = 220
    double y500 = 1e9;
    for (const auto& c : after) if (c.index == 500) y500 = c.screenY;
    EXPECT_DOUBLE_EQ(y500, 220.0);
}

TEST(ScrollModelTest, T5_JumpFarDoesNotMeasureAbove)
{
    Fake f(10000);
    auto m = makeModel(f);
    m.jumpTo(5000, 0.0);
    auto v = m.visible();
    ASSERT_FALSE(v.empty());
    EXPECT_EQ(v.front().index, 5000u);
    EXPECT_DOUBLE_EQ(v.front().screenY, 0.0);
    // 5000 之上无任何测量
    for (size_t i = 0; i < 5000; ++i) EXPECT_EQ(f.calls[i], 0) << i;
}

TEST(ScrollModelTest, T6_ScrollUpIntoUnmeasuredChunkMeasuresIt)
{
    Fake f(100, 100.0);
    auto m = makeModel(f);
    m.jumpTo(50, 10.0);
    (void)m.visible();
    EXPECT_EQ(f.calls[49], 0);
    m.scrollBy(-30.0);
    EXPECT_GE(f.calls[49], 1);
    EXPECT_EQ(m.anchorChunk(), 49u);
    EXPECT_DOUBLE_EQ(m.anchorOffset(), 80.0);
}

TEST(ScrollModelTest, T7_ScrollToEndAlignsLastChunkBottomExactly)
{
    Fake f(500, 137.0, 50.0, 7);
    auto m = makeModel(f, 4, 25.0, 600.0);
    m.scrollToEnd();
    auto v = m.visible();
    ASSERT_FALSE(v.empty());
    const auto& last = v.back();
    EXPECT_EQ(last.index, 499u);
    EXPECT_NEAR(last.screenY + last.height, 600.0, 1e-6);
}

TEST(ScrollModelTest, T8_ScrollPastEndClamps)
{
    Fake f(20, 100.0);
    auto m = makeModel(f);
    for (int i = 0; i < 100; ++i) m.scrollBy(300.0);
    auto v = m.visible();
    const auto& last = v.back();
    EXPECT_EQ(last.index, 19u);
    EXPECT_NEAR(last.screenY + last.height, 600.0, 1e-6);
    // 向上超过起点夹紧到 0
    for (int i = 0; i < 100; ++i) m.scrollBy(-300.0);
    EXPECT_DOUBLE_EQ(m.topY(), 0.0);
}

TEST(ScrollModelTest, T9_InvalidateAllKeepsAnchorAndRemeasures)
{
    Fake f(300, 100.0);
    auto m = makeModel(f);
    m.jumpTo(100, 20.0);
    (void)m.visible();
    const int callsBefore = f.totalCalls();
    f.real.assign(300, 160.0);        // 宽度变化后每块变高
    m.invalidateAll();
    EXPECT_EQ(m.measuredCount(), 0u);
    auto v = m.visible();
    EXPECT_EQ(m.anchorChunk(), 100u);
    EXPECT_DOUBLE_EQ(v.front().screenY, -20.0);
    EXPECT_DOUBLE_EQ(v.front().height, 160.0);
    EXPECT_GT(f.totalCalls(), callsBefore);
}

TEST(ScrollModelTest, T10_SetTopYRoundTrip)
{
    Fake f(1000, 100.0);
    auto m = makeModel(f);
    m.setTopY(12345.0);
    EXPECT_NEAR(m.topY(), 12345.0, 1e-6);
    EXPECT_EQ(m.anchorChunk(), 123u);
    EXPECT_NEAR(m.anchorOffset(), 45.0, 1e-6);
}

TEST(ScrollModelTest, T11_CalibrationAdaptsUnmeasuredEstimates)
{
    // 初始估 25px/行 → 100/块；实高 200/块，测量若干块后总高估计应趋近 200*n
    Fake f(1000, 200.0);
    auto m = makeModel(f, 4, 25.0);
    const double before = m.total();
    for (int i = 0; i < 20; ++i) { m.scrollBy(500.0); (void)m.visible(); }
    EXPECT_GT(m.total(), before * 1.5);
    EXPECT_NEAR(m.pxPerLine(), 50.0, 5.0);
}

TEST(ScrollModelTest, T12_AppendDoesNotMoveAnchor)
{
    Fake f(50, 100.0);
    auto m = makeModel(f);
    m.jumpTo(10, 5.0);
    (void)m.visible();
    f.real.resize(100, 100.0);
    f.calls.resize(100, 0);
    for (int i = 0; i < 50; ++i) m.appendChunk(4);
    EXPECT_EQ(m.count(), 100u);
    EXPECT_EQ(m.anchorChunk(), 10u);
    EXPECT_DOUBLE_EQ(m.anchorOffset(), 5.0);
    auto v = m.visible();
    EXPECT_DOUBLE_EQ(v.front().screenY, -5.0);
}

TEST(ScrollModelTest, T13_FrozenCalibrationKeepsTotalStable)
{
    Fake f(1000, 300.0);
    auto m = makeModel(f, 4, 25.0);
    m.setCalibrationFrozen(true);
    const double before = m.total();
    for (int i = 0; i < 10; ++i) { m.scrollBy(400.0); (void)m.visible(); }
    // 冻结期间只有已测量块自身的高度变化，未测量块估高不得整体平移
    EXPECT_DOUBLE_EQ(m.pxPerLine(), 25.0);
    EXPECT_LT(m.total(), before + 100 * 300.0);
}

TEST(ScrollModelTest, T14_MillionChunksPerformance)
{
    const size_t N = 1000000;
    Fake f(1, 100.0);  // 测量器按 i 取模
    ScrollModel m;
    m.setInitialPxPerLine(25.0);
    m.setViewportHeight(800);
    m.setMeasurer([](size_t) { return 100.0; });
    auto t0 = std::chrono::steady_clock::now();
    for (size_t i = 0; i < N; ++i) m.appendChunk(4);
    auto t1 = std::chrono::steady_clock::now();
    std::mt19937 g(3);
    for (int k = 0; k < 2000; ++k) {
        m.jumpTo(g() % N, 0.0);
        auto v = m.visible();
        ASSERT_FALSE(v.empty());
    }
    auto t2 = std::chrono::steady_clock::now();
    const double buildMs = std::chrono::duration<double, std::milli>(t1 - t0).count();
    const double jumpMs = std::chrono::duration<double, std::milli>(t2 - t1).count();
    std::printf("  [perf] 1M chunks build=%.1fms, 2000 random jumps=%.1fms\n", buildMs, jumpMs);
    EXPECT_LT(buildMs, 2000.0);
    EXPECT_LT(jumpMs, 2000.0);
    EXPECT_NEAR(m.total(), static_cast<double>(N) * 100.0, 1e9 * 0.0 + N * 100.0 * 0.01);
}
