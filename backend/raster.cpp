#include "raster.h"

#include <miniz.h>

#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_STATIC
#define STBI_NO_STDIO
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#define STBI_ONLY_GIF
#define STBI_ONLY_BMP
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#endif
#include <stb_image.h>
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

#include <algorithm>
#include <cmath>

namespace templide::backend::raster {
    namespace {
        const stbi_uc* data_of(const std::string& bytes) {
            return reinterpret_cast<const stbi_uc*>(bytes.data());
        }

        // 가로(horizontal)나 세로로 반지름 radius인 상자 평균을 낸다
        void box_blur(Raster& raster, int radius, bool horizontal) {
            const int length = horizontal ? raster.width : raster.height;
            const int lines = horizontal ? raster.height : raster.width;
            const float scale = 1.0f / static_cast<float>(2 * radius + 1);
            std::vector<float> line(static_cast<std::size_t>(length) * 3);
            for (int l = 0; l < lines; ++l) {
                const auto pixel = [&](int i) { return horizontal ? raster.at(i, l) : raster.at(l, i); };
                for (int i = 0; i < length; ++i) {
                    std::copy_n(pixel(i), 3, &line[static_cast<std::size_t>(i) * 3]);
                }
                const auto source = [&](int i) { return &line[static_cast<std::size_t>(std::clamp(i, 0, length - 1)) * 3]; };
                float sum[3] = {0, 0, 0};
                for (int i = -radius; i <= radius; ++i) {
                    for (int c = 0; c < 3; ++c) {
                        sum[c] += source(i)[c];
                    }
                }
                for (int i = 0; i < length; ++i) {
                    float* out = pixel(i);
                    for (int c = 0; c < 3; ++c) {
                        out[c] = sum[c] * scale;
                        sum[c] += source(i + radius + 1)[c] - source(i - radius)[c];
                    }
                }
            }
        }
    }

    std::optional<std::pair<int, int>> image_size(const std::string& bytes) {
        int width = 0;
        int height = 0;
        int channels = 0;
        if (!stbi_info_from_memory(data_of(bytes), static_cast<int>(bytes.size()), &width, &height, &channels)) {
            return std::nullopt;
        }
        return std::pair{width, height};
    }

    std::optional<Raster> decode(const std::string& bytes) {
        int width = 0;
        int height = 0;
        int channels = 0;
        stbi_uc* data = stbi_load_from_memory(data_of(bytes), static_cast<int>(bytes.size()), &width, &height, &channels, 4);
        if (data == nullptr) {
            return std::nullopt;
        }
        Raster raster(width, height);
        for (std::size_t i = 0; i < static_cast<std::size_t>(width) * height; ++i) {
            const float alpha = data[i * 4 + 3] / 255.0f;
            for (int c = 0; c < 3; ++c) {
                raster.pixels[i * 3 + c] = data[i * 4 + c] * alpha + 255 * (1 - alpha);
            }
        }
        stbi_image_free(data);
        return raster;
    }

    Raster halve(const Raster& raster) {
        Raster result(std::max(1, raster.width / 2), std::max(1, raster.height / 2));
        for (int y = 0; y < result.height; ++y) {
            for (int x = 0; x < result.width; ++x) {
                const int x0 = std::min(x * 2, raster.width - 1);
                const int x1 = std::min(x * 2 + 1, raster.width - 1);
                const int y0 = std::min(y * 2, raster.height - 1);
                const int y1 = std::min(y * 2 + 1, raster.height - 1);
                for (int c = 0; c < 3; ++c) {
                    result.at(x, y)[c] = (raster.at(x0, y0)[c] + raster.at(x1, y0)[c] + raster.at(x0, y1)[c] + raster.at(x1, y1)[c]) / 4;
                }
            }
        }
        return result;
    }

    // 상자 평균 세 번으로 가우스 흐림을 흉내 낸다
    void blur(Raster& raster, double sigma) {
        if (sigma < 0.5 || raster.width == 0 || raster.height == 0) {
            return;
        }
        constexpr int passes = 3;
        const double ideal = std::sqrt(12 * sigma * sigma / passes + 1);
        int lower = static_cast<int>(std::floor(ideal));
        if (lower % 2 == 0) {
            --lower;
        }
        const int upper = lower + 2;
        const int lower_count = static_cast<int>(std::lround((12 * sigma * sigma - passes * lower * lower - 4 * passes * lower - 3 * passes) / (-4.0 * lower - 4)));
        for (int pass = 0; pass < passes; ++pass) {
            const int radius = ((pass < lower_count ? lower : upper) - 1) / 2;
            box_blur(raster, radius, true);
            box_blur(raster, radius, false);
        }
    }

    void sample(const Raster& raster, double x, double y, float out[3]) {
        x = std::clamp(x, 0.0, static_cast<double>(raster.width - 1));
        y = std::clamp(y, 0.0, static_cast<double>(raster.height - 1));
        const int x0 = static_cast<int>(x);
        const int y0 = static_cast<int>(y);
        const int x1 = std::min(x0 + 1, raster.width - 1);
        const int y1 = std::min(y0 + 1, raster.height - 1);
        const auto fx = static_cast<float>(x - x0);
        const auto fy = static_cast<float>(y - y0);
        for (int c = 0; c < 3; ++c) {
            const float top = raster.at(x0, y0)[c] * (1 - fx) + raster.at(x1, y0)[c] * fx;
            const float bottom = raster.at(x0, y1)[c] * (1 - fx) + raster.at(x1, y1)[c] * fx;
            out[c] = top * (1 - fy) + bottom * fy;
        }
    }

    std::string encode_png(const Raster& raster) {
        std::vector<unsigned char> bytes(raster.pixels.size());
        for (std::size_t i = 0; i < bytes.size(); ++i) {
            bytes[i] = static_cast<unsigned char>(std::clamp(std::lround(raster.pixels[i]), 0L, 255L));
        }
        std::size_t size = 0;
        void* png = tdefl_write_image_to_png_file_in_memory_ex(bytes.data(), raster.width, raster.height, 3, &size, MZ_DEFAULT_LEVEL, MZ_FALSE);
        if (png == nullptr) {
            return "";
        }
        std::string result(static_cast<const char*>(png), size);
        mz_free(png);
        return result;
    }
}
