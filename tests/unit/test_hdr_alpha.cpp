/**
 * @file tests/unit/test_hdr_alpha.cpp
 * @brief Tests for the 2-bit alpha channel of the 10-bit HDR pixel formats.
 *
 * The bundled FFmpeg patch registers the DRM BGRA1010102/RGBA1010102 formats
 * and provides legacy swscale input and output handlers for them. These tests
 * cover the alpha channel of those handlers: opaque alpha when the source has
 * no alpha, 2-bit to 8-bit expansion, output support and an exact 1:1 round
 * trip of all four 2-bit alpha values.
 */
#include "../tests_common.h"

extern "C" {
#include <libavutil/pixfmt.h>
#include <libswscale/swscale.h>
}

#include <cstdint>
#include <cstring>

namespace {
  /**
   * @brief Pack a pixel into the BGRA1010102LE memory layout.
   * @param r Red channel (10 bits).
   * @param g Green channel (10 bits).
   * @param b Blue channel (10 bits).
   * @param a Alpha channel (2 bits).
   * @return The packed little-endian pixel value.
   */
  std::uint32_t pack_bgra1010102(std::uint32_t r, std::uint32_t g, std::uint32_t b, std::uint32_t a) {
    return ((b & 0x3FF) << 22) | ((g & 0x3FF) << 12) | ((r & 0x3FF) << 2) | (a & 0x3);
  }

  /**
   * @brief Read a BGRA1010102LE pixel from a row.
   * @param row Pointer to the row data.
   * @param index Pixel index.
   * @return The packed little-endian pixel value.
   */
  std::uint32_t pixel_at(const std::uint8_t *row, int index) {
    std::uint32_t pixel;
    std::memcpy(&pixel, row + index * 4, sizeof(pixel));
    return pixel;
  }

  /**
   * @brief Extract a 10-bit channel from a packed 1010102 pixel.
   * @param pixel The packed pixel value.
   * @param shift Bit offset of the channel.
   * @return The 10-bit channel value.
   */
  std::uint32_t channel_at(std::uint32_t pixel, int shift) {
    return (pixel >> shift) & 0x3FF;
  }

  /**
   * @brief The flag combinations exercised by the RGB output writers.
   */
  constexpr int output_flags[] = { SWS_POINT, SWS_POINT | SWS_FULL_CHR_H_INT };
}

/**
 * @brief Both 1010102 formats must be advertised as swscale input and output.
 */
TEST(HdrAlpha, FormatSupport) {
  EXPECT_EQ(sws_isSupportedInput(AV_PIX_FMT_BGRA1010102LE), 1);
  EXPECT_EQ(sws_isSupportedOutput(AV_PIX_FMT_BGRA1010102LE), 1);
  EXPECT_EQ(sws_isSupportedOutput(AV_PIX_FMT_RGBA1010102LE), 1);
}

/**
 * @brief A 2-bit alpha source must expand to the 8-bit alpha levels 0/85/171/255.
 */
TEST(HdrAlpha, TwoBitAlphaExpandsToEightBit) {
  constexpr int width = 4;
  std::uint32_t src[width];
  for (int i = 0; i < width; ++i) {
    src[i] = pack_bgra1010102(1023, 0, 0, i);
  }

  const std::uint8_t *src_data[4] = { reinterpret_cast<const std::uint8_t *>(src), nullptr, nullptr, nullptr };
  int src_stride[4] = { width * 4, 0, 0, 0 };

  std::uint8_t dst_y[width] = {};
  std::uint8_t dst_u[width] = {};
  std::uint8_t dst_v[width] = {};
  std::uint8_t dst_a[width] = {};
  std::uint8_t *dst_data[4] = { dst_y, dst_u, dst_v, dst_a };
  int dst_stride[4] = { width, 2, 2, width };

  auto sws = sws_getContext(width, 1, AV_PIX_FMT_BGRA1010102LE,
                            width, 1, AV_PIX_FMT_YUVA420P,
                            SWS_POINT, nullptr, nullptr, nullptr);
  ASSERT_NE(sws, nullptr);

  ASSERT_EQ(sws_scale(sws, src_data, src_stride, 0, 1, dst_data, dst_stride), 1);
  sws_freeContext(sws);

  // 170 expands to 171 because FFmpeg rounds the 14-bit internal value, exactly
  // like it does for an 8-bit RGBA source with alpha 170.
  constexpr std::uint8_t expected[4] = { 0x00, 0x55, 0xAB, 0xFF };
  for (int i = 0; i < width; ++i) {
    EXPECT_EQ(dst_a[i], expected[i]) << "pixel " << i;
  }
}

/**
 * @brief An 8-bit alpha source must map onto the four 2-bit alpha levels.
 */
TEST(HdrAlpha, EightBitAlphaMapsToTwoBit) {
  constexpr int width = 4;
  std::uint8_t src_y[width] = { 128, 128, 128, 128 };
  std::uint8_t src_u[2] = { 128, 128 };
  std::uint8_t src_v[2] = { 128, 128 };
  std::uint8_t src_a[width] = { 0x55, 0xAA, 0xFF, 0x00 };
  const std::uint8_t *src_data[4] = { src_y, src_u, src_v, src_a };
  int src_stride[4] = { width, 2, 2, width };

  for (int flags : output_flags) {
    constexpr int dst_width = 8;
    std::uint8_t dst[dst_width * 4] = {};
    std::uint8_t *dst_data[4] = { dst, nullptr, nullptr, nullptr };
    int dst_stride[4] = { dst_width * 4, 0, 0, 0 };

    auto sws = sws_getContext(width, 1, AV_PIX_FMT_YUVA420P,
                              dst_width, 1, AV_PIX_FMT_BGRA1010102LE,
                              flags, nullptr, nullptr, nullptr);
    ASSERT_NE(sws, nullptr);
    ASSERT_EQ(sws_scale(sws, src_data, src_stride, 0, 1, dst_data, dst_stride), 1);
    sws_freeContext(sws);

    constexpr std::uint32_t expected[4] = { 1, 2, 3, 0 };
    for (int i = 0; i < width; ++i) {
      EXPECT_EQ(pixel_at(dst, i * 2) & 0x3, expected[i]) << "flags " << flags << " pixel " << i;
      EXPECT_EQ(pixel_at(dst, i * 2 + 1) & 0x3, expected[i]) << "flags " << flags << " pixel " << i;
    }
  }
}

/**
 * @brief A source without alpha must produce opaque (max) 2-bit alpha.
 */
TEST(HdrAlpha, OpaqueAlphaWithoutSource) {
  constexpr int width = 4;
  std::uint8_t src_y[width] = { 128, 128, 128, 128 };
  std::uint8_t src_u[2] = { 128, 128 };
  std::uint8_t src_v[2] = { 128, 128 };
  const std::uint8_t *src_data[4] = { src_y, src_u, src_v, nullptr };
  int src_stride[4] = { width, 2, 2, 0 };

  for (int flags : output_flags) {
    std::uint8_t dst[width * 4] = {};
    std::uint8_t *dst_data[4] = { dst, nullptr, nullptr, nullptr };
    int dst_stride[4] = { width * 4, 0, 0, 0 };

    auto sws = sws_getContext(width, 1, AV_PIX_FMT_YUV420P,
                              width, 1, AV_PIX_FMT_BGRA1010102LE,
                              flags, nullptr, nullptr, nullptr);
    ASSERT_NE(sws, nullptr);
    ASSERT_EQ(sws_scale(sws, src_data, src_stride, 0, 1, dst_data, dst_stride), 1);
    sws_freeContext(sws);

    for (int i = 0; i < width; ++i) {
      EXPECT_EQ(pixel_at(dst, i) & 0x3, 3) << "flags " << flags << " pixel " << i;
    }
  }
}

/**
 * @brief Scaling must preserve all four 2-bit alpha values without loss.
 */
TEST(HdrAlpha, TwoBitRoundTripPreservesAlpha) {
  constexpr int src_width = 4;
  constexpr int dst_width = 8;

  std::uint32_t src[src_width];
  for (int i = 0; i < src_width; ++i) {
    src[i] = pack_bgra1010102(100 + i, 200 + i, 300 + i, i);
  }

  const std::uint8_t *src_data[4] = { reinterpret_cast<const std::uint8_t *>(src), nullptr, nullptr, nullptr };
  int src_stride[4] = { src_width * 4, 0, 0, 0 };

  std::uint8_t dst[dst_width * 4] = {};
  std::uint8_t *dst_data[4] = { dst, nullptr, nullptr, nullptr };
  int dst_stride[4] = { dst_width * 4, 0, 0, 0 };

  auto sws = sws_getContext(src_width, 1, AV_PIX_FMT_BGRA1010102LE,
                            dst_width, 1, AV_PIX_FMT_BGRA1010102LE,
                            SWS_POINT, nullptr, nullptr, nullptr);
  ASSERT_NE(sws, nullptr);
  ASSERT_EQ(sws_scale(sws, src_data, src_stride, 0, 1, dst_data, dst_stride), 1);
  sws_freeContext(sws);

  for (int i = 0; i < src_width; ++i) {
    EXPECT_EQ(pixel_at(dst, i * 2) & 0x3, static_cast<std::uint32_t>(i)) << "pixel " << i;
    EXPECT_EQ(pixel_at(dst, i * 2 + 1) & 0x3, static_cast<std::uint32_t>(i)) << "pixel " << i;
  }
}

/**
 * @brief The RGBA and BGRA writers must place the channels at opposite ends.
 */
TEST(HdrAlpha, ChannelOrder) {
  constexpr int width = 4;
  std::uint32_t src[width];
  for (int i = 0; i < width; ++i) {
    src[i] = pack_bgra1010102(1023, 0, 0, 3);
  }

  const std::uint8_t *src_data[4] = { reinterpret_cast<const std::uint8_t *>(src), nullptr, nullptr, nullptr };
  int src_stride[4] = { width * 4, 0, 0, 0 };

  for (auto dst_format : { AV_PIX_FMT_BGRA1010102LE, AV_PIX_FMT_RGBA1010102LE }) {
    std::uint8_t dst[width * 4] = {};
    std::uint8_t *dst_data[4] = { dst, nullptr, nullptr, nullptr };
    int dst_stride[4] = { width * 4, 0, 0, 0 };

    auto sws = sws_getContext(width, 1, AV_PIX_FMT_BGRA1010102LE,
                              width, 1, dst_format,
                              SWS_POINT, nullptr, nullptr, nullptr);
    ASSERT_NE(sws, nullptr);
    ASSERT_EQ(sws_scale(sws, src_data, src_stride, 0, 1, dst_data, dst_stride), 1);
    sws_freeContext(sws);

    for (int i = 0; i < width; ++i) {
      const auto pixel = pixel_at(dst, i);
      const auto red = dst_format == AV_PIX_FMT_RGBA1010102LE ? channel_at(pixel, 22) : channel_at(pixel, 2);
      const auto green = channel_at(pixel, 12);
      const auto blue = dst_format == AV_PIX_FMT_RGBA1010102LE ? channel_at(pixel, 2) : channel_at(pixel, 22);
      EXPECT_GT(red, 900u) << "pixel " << i;
      EXPECT_LT(green, 100u) << "pixel " << i;
      EXPECT_LT(blue, 100u) << "pixel " << i;
    }
  }
}
