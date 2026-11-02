// tests/viewer/ScanBench.cpp
//
// Spec: specs/模块-viewer/01-分块扫描.md §6
// 用途：测量读文件 + 扫描耗时（手动运行，不入 ctest）。用法：ScanBench <md文件>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <cstdio>
#include "DocumentLoader.h"

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    if (argc < 2) return 2;
    QElapsedTimer t;
    t.start();
    std::shared_ptr<QByteArray> buf;
    auto r = viewer::DocumentLoader::loadSync(argv[1], buf);
    std::printf("load+scan %lld ms: bytes=%lld chunks=%zu headings=%zu lines=%u err=%s\n",
                (long long)t.elapsed(), buf ? (long long)buf->size() : 0LL, r.chunks.size(),
                r.headings.size(), r.lineCount, qPrintable(r.error));
    return 0;
}
