#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace lumena::image {

/// A single 8-bit RGBA pixel.
struct Rgba {
    std::uint8_t r = 0;
    std::uint8_t g = 0;
    std::uint8_t b = 0;
    std::uint8_t a = 255;
};

/// An in-memory image: tightly-packed 8-bit RGBA pixels in row-major order,
/// top-left origin. Four channels per pixel, so the backing buffer has
/// exactly `width * height * 4` bytes.
///
/// The type is exception-free across its public boundary: file loading returns
/// std::optional and the raw-buffer constructor validates its input, leaving
/// the object `empty()` if the buffer size does not match the dimensions.
class Image {
public:
    /// Number of channels stored per pixel (R, G, B, A).
    static constexpr int kChannels = 4;

    /// Constructs an empty (0x0) image.
    Image() = default;

    /// Constructs an image from a raw, tightly-packed RGBA8 buffer.
    ///
    /// Intended for hosts (e.g. the JUCE plugin) that already hold decoded
    /// pixel data and want to hand it over without touching the filesystem.
    ///
    /// If `width`/`height` are non-positive or `rgba.size()` does not equal
    /// `width * height * 4`, the image is left empty rather than throwing.
    Image(int width, int height, std::vector<std::uint8_t> rgba);

    /// Loads and decodes an image file (PNG, JPEG, ...) via stb_image, forcing
    /// RGBA output. Returns std::nullopt if the file cannot be read or decoded.
    static std::optional<Image> loadFromFile(const std::string& path);

    /// Decodes an image from an in-memory encoded buffer (e.g. the bytes of a
    /// PNG/JPEG file). Returns std::nullopt on failure.
    static std::optional<Image> loadFromEncoded(const std::uint8_t* data,
                                                std::size_t size);

    int width() const noexcept { return width_; }
    int height() const noexcept { return height_; }

    /// True when the image holds no pixels (default-constructed or a failed
    /// raw-buffer construction).
    bool empty() const noexcept { return width_ <= 0 || height_ <= 0; }

    /// Total number of pixels (`width * height`).
    std::size_t pixelCount() const noexcept {
        return static_cast<std::size_t>(width_) * static_cast<std::size_t>(height_);
    }

    /// Returns the pixel at (x, y). Behaviour is undefined if the coordinates
    /// are out of range; callers must stay within [0, width) x [0, height).
    Rgba at(int x, int y) const noexcept;

    /// Read-only access to the underlying RGBA8 buffer.
    const std::vector<std::uint8_t>& data() const noexcept { return pixels_; }

private:
    int width_ = 0;
    int height_ = 0;
    std::vector<std::uint8_t> pixels_;  // RGBA8, size == width_ * height_ * 4
};

} // namespace lumena::image
