#include "image/Image.h"

#include <utility>

// stb_image pulls in the single-header implementation in exactly this one
// translation unit. Silence the third-party header's warnings so the library's
// own -Wall -Wextra -Wpedantic stays meaningful for our code.
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wsign-conversion"
#pragma GCC diagnostic ignored "-Wconversion"
#pragma GCC diagnostic ignored "-Wcast-qual"
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wpedantic"
#endif

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic pop
#endif

namespace lumena::image {

namespace {

// Wraps a decoded stb buffer into an Image, copying into an owned vector and
// releasing the stb allocation. `data` may be null (decode failure).
std::optional<Image> makeFromStb(unsigned char* data, int width, int height) {
    if (data == nullptr || width <= 0 || height <= 0) {
        if (data != nullptr) {
            stbi_image_free(data);
        }
        return std::nullopt;
    }

    const std::size_t byteCount =
        static_cast<std::size_t>(width) * static_cast<std::size_t>(height) *
        static_cast<std::size_t>(Image::kChannels);

    std::vector<std::uint8_t> pixels(data, data + byteCount);
    stbi_image_free(data);

    return Image(width, height, std::move(pixels));
}

} // namespace

Image::Image(int width, int height, std::vector<std::uint8_t> rgba) {
    if (width <= 0 || height <= 0) {
        return;  // stays empty
    }

    const std::size_t expected =
        static_cast<std::size_t>(width) * static_cast<std::size_t>(height) *
        static_cast<std::size_t>(kChannels);

    if (rgba.size() != expected) {
        return;  // size mismatch -> stays empty, no throw
    }

    width_ = width;
    height_ = height;
    pixels_ = std::move(rgba);
}

std::optional<Image> Image::loadFromFile(const std::string& path) {
    int width = 0;
    int height = 0;
    int sourceChannels = 0;
    unsigned char* data =
        stbi_load(path.c_str(), &width, &height, &sourceChannels, kChannels);
    return makeFromStb(data, width, height);
}

std::optional<Image> Image::loadFromEncoded(const std::uint8_t* data,
                                            std::size_t size) {
    if (data == nullptr || size == 0) {
        return std::nullopt;
    }

    int width = 0;
    int height = 0;
    int sourceChannels = 0;
    unsigned char* decoded = stbi_load_from_memory(
        data, static_cast<int>(size), &width, &height, &sourceChannels,
        kChannels);
    return makeFromStb(decoded, width, height);
}

Rgba Image::at(int x, int y) const noexcept {
    const std::size_t index =
        (static_cast<std::size_t>(y) * static_cast<std::size_t>(width_) +
         static_cast<std::size_t>(x)) *
        static_cast<std::size_t>(kChannels);

    return Rgba{pixels_[index + 0], pixels_[index + 1], pixels_[index + 2],
                pixels_[index + 3]};
}

} // namespace lumena::image
