
#include "fl/rectangular_draw_buffer.h"
#include "fl/namespace.h"
#include "rgbw.h"

namespace fl {

DrawItem::DrawItem(uint8_t pin, uint16_t numLeds, bool is_rgbw)
    : mPin(pin), mIsRgbw(is_rgbw) {
    if (is_rgbw) {
        numLeds = Rgbw::size_as_rgb(numLeds);
    } else {
        numLeds = numLeds;
    }
    mNumBytes = numLeds * 3;
}

Slice<uint8_t> RectangularDrawBuffer::getLedsBufferBytesForPin(uint8_t pin, bool clear_first) {
    auto it = mPinToLedSegment.find(pin);
    if (it == mPinToLedSegment.end()) {
        FASTLED_ASSERT(false, "Pin not found in RectangularDrawBuffer");
        return fl::Slice<uint8_t>();
    }
    fl::Slice<uint8_t> slice = it->second;
    if (clear_first) {
        memset(slice.data(), 0, slice.size());
    }
    return slice;
}

void RectangularDrawBuffer::onQueuingStart() {
    if (mQueueState == QUEUEING) {
        return;
    }
    mQueueState = QUEUEING;
    mPinToLedSegment.clear();
    mDrawList.swap(mPrevDrawList);
    mDrawList.clear();
    if (!mAllLedsBufferUint8.empty()) {
        memset(&mAllLedsBufferUint8.front(), 0, mAllLedsBufferUint8.size());
        mAllLedsBufferUint8.clear();
    }
}

void RectangularDrawBuffer::queue(const DrawItem &item) {
    mDrawList.push_back(item);
}

void RectangularDrawBuffer::onQueuingDone() {
    if (mQueueState == QUEUE_DONE) {
        return;
    }
    mQueueState = QUEUE_DONE;
    // iterator through the current draw objects and calculate the total
    // number of bytes (representing RGB or RGBW) that will be drawn this frame.
    uint32_t total_bytes = 0;
    uint32_t max_bytes_in_strip = 0;
    uint32_t num_strips = 0;
    getBlockInfo(&num_strips, &max_bytes_in_strip, &total_bytes);
    mAllLedsBufferUint8.resize(total_bytes);
    memset(&mAllLedsBufferUint8.front(), 0, mAllLedsBufferUint8.size());
    uint32_t offset = 0;
    for (auto it = mDrawList.begin(); it != mDrawList.end(); ++it) {
        uint8_t pin = it->mPin;
        Slice<uint8_t> slice(&mAllLedsBufferUint8.front() + offset,
                             max_bytes_in_strip);
        mPinToLedSegment[pin] = slice;
        offset += max_bytes_in_strip;
    }
}

uint32_t RectangularDrawBuffer::getMaxBytesInStrip() const {
    uint32_t max_bytes = 0;
    for (auto it = mDrawList.begin(); it != mDrawList.end(); ++it) {
        max_bytes = MAX(max_bytes, it->mNumBytes);
    }
    return max_bytes;
}

uint32_t RectangularDrawBuffer::getTotalBytes() const {
    uint32_t num_strips = mDrawList.size();
    uint32_t max_bytes = getMaxBytesInStrip();
    return num_strips * max_bytes;
}

void RectangularDrawBuffer::getBlockInfo(uint32_t *num_strips,
                                         uint32_t *bytes_per_strip,
                                         uint32_t *total_bytes) const {
    *num_strips = mDrawList.size();
    *bytes_per_strip = getMaxBytesInStrip();
    *total_bytes = (*num_strips) * (*bytes_per_strip);
}

} // namespace fl