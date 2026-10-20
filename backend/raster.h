#pragma once

#include <optional>
#include <string>
#include <utility>
#include <vector>

/** backdrop_blur에 사용하는 래스터 이미지의 디코딩과 처리를 담당한다. */
namespace templide::backend::raster {
    /** 불투명한 RGB 이미지. 각 채널은 0부터 255까지의 값을 사용한다. */
    struct Raster {
        int width = 0;
        int height = 0;
        std::vector<float> pixels; // (y * width + x) * 3 + 채널

        Raster() = default;
        Raster(int width, int height) : width(width), height(height), pixels(static_cast<std::size_t>(width) * height * 3) {}

        float* at(int x, int y) {
            return &pixels[(static_cast<std::size_t>(y) * width + x) * 3];
        }
        const float* at(int x, int y) const {
            return &pixels[(static_cast<std::size_t>(y) * width + x) * 3];
        }
    };

    // png, jpg, gif, bmp 파일의 가로세로 크기
    std::optional<std::pair<int, int>> image_size(const std::string& bytes);

    // png, jpg, gif, bmp 파일을 읽는다. 투명한 부분은 흰 바탕에 합성한다
    std::optional<Raster> decode(const std::string& bytes);

    // 2x2 픽셀의 평균으로 가로세로를 반으로 줄인다
    Raster halve(const Raster& raster);

    // 가우스 흐림. sigma는 픽셀 단위이고, 가장자리 바깥은 가장자리 색이 이어진다고 본다
    void blur(Raster& raster, double sigma);

    // 픽셀 중심이 정수 좌표인 이중 선형 보간. 범위 밖은 가장자리 색이다
    void sample(const Raster& raster, double x, double y, float out[3]);

    std::string encode_png(const Raster& raster);
}
