#pragma once
#include "bitmap_view.h"
#include <functional>
namespace vj {
struct BitmapUiContext {
    unsigned (*createTexture)(const void*, int, int) = nullptr;
    void (*destroyTexture)(unsigned) = nullptr;
    std::function<void(const OpObject&, const OpRenderOptions&)> openObject;
    std::string exportDir;
};
void showBitmapAt(DebugSession&, unsigned address);
void drawBitmap(DebugSession&, const BitmapUiContext&);
void releaseBitmap();
namespace bitmap_test { const BitmapViewModel& model(); }
}
