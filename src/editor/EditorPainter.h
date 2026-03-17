#pragma once
#include <QtGlobal>

class QPainter;
class EditorLayout;
class Document;

class EditorPainter {
public:
    EditorPainter();

    void paint(QPainter* painter, EditorLayout* layout, Document* doc,
               int firstLine, int lastLine,
               int gutterWidth, qreal scrollY);
};
