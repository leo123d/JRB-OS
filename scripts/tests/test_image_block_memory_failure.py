"""ImageBlock must forward decoder failures without poisoning the next render.

This used to live in test_airpage_display_retry.py alongside AirPage-specific
checks. The AirPage app was removed with the app suite, but the ImageBlock
contract it exercised is still production code, so the check was kept.
"""
from pathlib import Path
import unittest
from test_reading_ui_regressions import method, run_cpp

ROOT = Path(__file__).resolve().parents[2]


class ImageBlockMemoryFailureTest(unittest.TestCase):
    def test_image_block_forwards_memory_failure_without_poisoning_retry(self):
        source = (ROOT / 'lib/Epub/Epub/blocks/ImageBlock.cpp').read_text()
        decoder = (ROOT / 'lib/Epub/Epub/converters/ImageToFramebufferDecoder.h').read_text()
        types = decoder[decoder.index('enum class DecodeOutput'):decoder.index('class ImageToFramebufferDecoder')]
        run_cpp(r'''
#include <cassert>
#include <cstddef>
#include <cstdint>
#include "CancelCheck.h"
#include <string>
#define LOG_DBG(...) ((void)0)
#define LOG_ERR(...) ((void)0)
''' + types + r'''
struct FontCacheManager { bool isScanning() { return false; } };
struct GfxRenderer {
 FontCacheManager* getFontCacheManager() { return nullptr; }
 int getScreenWidth() { return 800; }
 int getScreenHeight() { return 480; }
 bool glyphIntersectsStrip(int,int,int,int) { return true; }
};
struct HalFile { size_t size() { return 42; } };
struct StorageStub { bool openFileForRead(const char*,const std::string&,HalFile&) { return true; } } Storage;
static bool remembered = false;
bool imageFailedThisRender(const std::string&) { return remembered; }
void rememberImageFailure(const std::string&) { remembered = true; }
std::string getCachePath(const std::string&) { return "image.pxc"; }
struct ImageBlock {
 enum class PixelCachePolicy { Stream };
 std::string imagePath = "image.jpg", srcPath;
 int width=100, height=100;
 bool hasValidCache() { return false; }
 bool ensureExtracted(CancelCheck) { return true; }
 void renderPlaceholder(GfxRenderer&,int,int) {}
 bool bilinearScalingEnabled() { return false; }
 bool renderInternal(GfxRenderer&,int,int,PixelCachePolicy,DecodeOutput,ImageRenderError*,CancelCheck={});
};
bool renderFromCache(GfxRenderer&,const std::string&,int,int,int,int,ImageBlock::PixelCachePolicy) { return false; }
struct ImageToFramebufferDecoder {
 bool fail = true;
 ImageRenderError failure=ImageRenderError::OutOfMemory;
 bool decodeToFramebuffer(const std::string&,GfxRenderer&,const RenderConfig& config) {
   if (config.error) *config.error = fail ? failure : ImageRenderError::None;
   return !fail;
 }
};
static ImageToFramebufferDecoder decoder;
struct ImageDecoderFactory { static ImageToFramebufferDecoder* getDecoder(const std::string&) { return &decoder; } };
''' + method(source, 'bool ImageBlock::renderInternal(') + r'''
int main() {
 ImageBlock block;
 GfxRenderer renderer;
 ImageRenderError error = ImageRenderError::None;
 assert(!block.renderInternal(renderer,0,0,ImageBlock::PixelCachePolicy::Stream,
                              DecodeOutput::FrameBufferAndCache,&error));
 assert(error == ImageRenderError::OutOfMemory && !remembered);
 decoder.failure = ImageRenderError::Failed;
 assert(!block.renderInternal(renderer,0,0,ImageBlock::PixelCachePolicy::Stream,
                             DecodeOutput::CacheOnly,&error));
 assert(!remembered); // Optional cache allocation/I/O failure must permit foreground retry.
 decoder.fail = false;
 assert(block.renderInternal(renderer,0,0,ImageBlock::PixelCachePolicy::Stream,
                             DecodeOutput::FrameBufferAndCache,&error));
 assert(error == ImageRenderError::None);
 CancelCheck cancelled{nullptr,[](void*){return true;}};
 assert(!block.renderInternal(renderer,0,0,ImageBlock::PixelCachePolicy::Stream,
                             DecodeOutput::FrameBufferAndCache,&error,cancelled));
 assert(error == ImageRenderError::Cancelled && !remembered);
 assert(!block.renderInternal(renderer,-1,0,ImageBlock::PixelCachePolicy::Stream,
                              DecodeOutput::FrameBufferAndCache,&error));
 assert(error == ImageRenderError::Failed);
}
''', (ROOT / 'lib/Memory',))
