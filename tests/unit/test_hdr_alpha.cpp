/**
 * @file tests/unit/test_hdr_alpha.cpp
 * @brief Tests for the 2-bit alpha channel of the 10-bit HDR pixel formats.
 *
 * The bundled FFmpeg patch registers the DRM BGRA1010102/RGBA1010102 formats.
 * swscale implements only the little-endian memory layout; the big-endian
 * variants exist in libavutil for API completeness but are not supported by
 * any swscale backend. These tests cover the alpha channel of the LE handlers:
 * opaque alpha when the source has no alpha, 2-bit to 8-bit expansion, output
 * support and an exact 1:1 round trip of all four 2-bit alpha values.
 *
 * The formats are little-endian and so are the helpers below, hence the
 * fixture skips the suite on big-endian hosts.
 */
#include "../tests_common.h"

extern "C" {
#include <libavutil/pixdesc.h>
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
   * @brief Whether the host stores multi-byte values in big-endian order.
   * @return true on a big-endian host.
   */
  constexpr bool is_big_endian() {
#if defined(__BYTE_ORDER__) && defined(__ORDER_BIG_ENDIAN__)
    return __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__;
#else
    return false;
#endif
  }

  /**
   * @brief The flag combinations exercised by the RGB output writers.
   */
  constexpr int output_flags[] = { SWS_POINT, SWS_POINT | SWS_FULL_CHR_H_INT };

  /**
   * @brief Convert a single row between two pixel formats with swscale.
   * @param src_format Source pixel format.
   * @param src_data Source plane pointers.
   * @param src_stride Source plane strides.
   * @param src_width Source width.
   * @param dst_format Destination pixel format.
   * @param dst_data Destination plane pointers.
   * @param dst_stride Destination plane strides.
   * @param dst_width Destination width.
   * @param flags swscale flags.
   * @return true when the context was created and the row was converted.
   */
  bool convert_row(enum AVPixelFormat src_format,
                   const std::uint8_t *const src_data[4],
                   const int src_stride[4],
                   int src_width,
                   enum AVPixelFormat dst_format,
                   std::uint8_t *const dst_data[4],
                   const int dst_stride[4],
                   int dst_width,
                   int flags) {
    util::safe_ptr<SwsContext, sws_freeContext> sws {
      sws_getContext(src_width, 1, src_format, dst_width, 1, dst_format,
                     flags, nullptr, nullptr, nullptr)
    };
    if (!sws) {
      ADD_FAILURE() << "sws_getContext failed";
      return false;
    }
    return sws_scale(sws.get(), src_data, src_stride, 0, 1, dst_data, dst_stride) == 1;
  }
}

/**
 * @brief Fixture for the 1010102 alpha tests.
 */
class HdrAlpha: public ::testing::Test {
protected:
  void SetUp() override {
    if (is_big_endian()) {
      GTEST_SKIP() << "the 1010102 formats and test helpers are little-endian";
    }
  }
};

/**
 * @brief The little-endian formats must be advertised as swscale input and output.
 */
TEST_F(HdrAlpha, FormatSupport) {
  EXPECT_EQ(sws_isSupportedInput(AV_PIX_FMT_BGRA1010102LE), 1);
  EXPECT_EQ(sws_isSupportedInput(AV_PIX_FMT_RGBA1010102LE), 1);
  EXPECT_EQ(sws_isSupportedOutput(AV_PIX_FMT_BGRA1010102LE), 1);
  EXPECT_EQ(sws_isSupportedOutput(AV_PIX_FMT_RGBA1010102LE), 1);
}

/**
 * @brief The big-endian variants are libavutil-only and swscale must say so.
 */
TEST_F(HdrAlpha, BigEndianVariantsUnsupported) {
  EXPECT_NE(av_pix_fmt_desc_get(AV_PIX_FMT_BGRA1010102BE), nullptr);
  EXPECT_NE(av_pix_fmt_desc_get(AV_PIX_FMT_RGBA1010102BE), nullptr);

  EXPECT_EQ(sws_isSupportedInput(AV_PIX_FMT_BGRA1010102BE), 0);
  EXPECT_EQ(sws_isSupportedOutput(AV_PIX_FMT_BGRA1010102BE), 0);
  EXPECT_EQ(sws_isSupportedInput(AV_PIX_FMT_RGBA1010102BE), 0);
  EXPECT_EQ(sws_isSupportedOutput(AV_PIX_FMT_RGBA1010102BE), 0);

  EXPECT_EQ(sws_test_format(AV_PIX_FMT_BGRA1010102BE, 0), 0);
  EXPECT_EQ(sws_test_format(AV_PIX_FMT_BGRA1010102BE, 1), 0);
  EXPECT_EQ(sws_test_format(AV_PIX_FMT_RGBA1010102BE, 0), 0);
  EXPECT_EQ(sws_test_format(AV_PIX_FMT_RGBA1010102BE, 1), 0);
}

/**
 * @brief A 2-bit alpha source must expand to the 8-bit alpha levels 0/85/171/255.
 */
TEST_F(HdrAlpha, TwoBitAlphaExpandsToEightBit) {
  constexpr int width = 4;
  std::uint32_t src[width];
  for (int i = 0; i < width; ++i) {
    src[i] = pack_bgra1010102(1023, 0, 0, i);
  }

  const std::uint8_t *src_data[4] = { reinterpret_cast<const std::uint8_t *>(src), nullptr, nullptr, nullptr };
  const int src_stride[4] = { width * 4, 0, 0, 0 };

  std::uint8_t dst_y[width] = {};
  std::uint8_t dst_u[width] = {};
  std::uint8_t dst_v[width] = {};
  std::uint8_t dst_a[width] = {};
  std::uint8_t *dst_data[4] = { dst_y, dst_u, dst_v, dst_a };
  const int dst_stride[4] = { width, 2, 2, width };

  ASSERT_TRUE(convert_row(AV_PIX_FMT_BGRA1010102LE, src_data, src_stride, width,
                          AV_PIX_FMT_YUVA420P, dst_data, dst_stride, width, SWS_POINT));

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
TEST_F(HdrAlpha, EightBitAlphaMapsToTwoBit) {
  constexpr int width = 4;
  std::uint8_t src_y[width] = { 128, 128, 128, 128 };
  std::uint8_t src_u[2] = { 128, 128 };
  std::uint8_t src_v[2] = { 128, 128 };
  std::uint8_t src_a[width] = { 0x55, 0xAA, 0xFF, 0x00 };
  const std::uint8_t *src_data[4] = { src_y, src_u, src_v, src_a };
  const int src_stride[4] = { width, 2, 2, width };

  for (int flags : output_flags) {
    constexpr int dst_width = 8;
    std::uint8_t dst[dst_width * 4] = {};
    std::uint8_t *dst_data[4] = { dst, nullptr, nullptr, nullptr };
    const int dst_stride[4] = { dst_width * 4, 0, 0, 0 };

    ASSERT_TRUE(convert_row(AV_PIX_FMT_YUVA420P, src_data, src_stride, width,
                            AV_PIX_FMT_BGRA1010102LE, dst_data, dst_stride, dst_width, flags)) << "flags " << flags;

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
TEST_F(HdrAlpha, OpaqueAlphaWithoutSource) {
  constexpr int width = 4;
  std::uint8_t src_y[width] = { 128, 128, 128, 128 };
  std::uint8_t src_u[2] = { 128, 128 };
  std::uint8_t src_v[2] = { 128, 128 };
  const std::uint8_t *src_data[4] = { src_y, src_u, src_v, nullptr };
  const int src_stride[4] = { width, 2, 2, 0 };

  for (int flags : output_flags) {
    std::uint8_t dst[width * 4] = {};
    std::uint8_t *dst_data[4] = { dst, nullptr, nullptr, nullptr };
    const int dst_stride[4] = { width * 4, 0, 0, 0 };

    ASSERT_TRUE(convert_row(AV_PIX_FMT_YUV420P, src_data, src_stride, width,
                            AV_PIX_FMT_BGRA1010102LE, dst_data, dst_stride, width, flags)) << "flags " << flags;

    for (int i = 0; i < width; ++i) {
      EXPECT_EQ(pixel_at(dst, i) & 0x3, 3) << "flags " << flags << " pixel " << i;
    }
  }
}

/**
 * @brief Scaling must preserve all four 2-bit alpha values without loss.
 */
TEST_F(HdrAlpha, TwoBitRoundTripPreservesAlpha) {
  constexpr int src_width = 4;
  constexpr int dst_width = 8;

  std::uint32_t src[src_width];
  for (int i = 0; i < src_width; ++i) {
    src[i] = pack_bgra1010102(100 + i, 200 + i, 300 + i, i);
  }

  const std::uint8_t *src_data[4] = { reinterpret_cast<const std::uint8_t *>(src), nullptr, nullptr, nullptr };
  const int src_stride[4] = { src_width * 4, 0, 0, 0 };

  std::uint8_t dst[dst_width * 4] = {};
  std::uint8_t *dst_data[4] = { dst, nullptr, nullptr, nullptr };
  const int dst_stride[4] = { dst_width * 4, 0, 0, 0 };

  ASSERT_TRUE(convert_row(AV_PIX_FMT_BGRA1010102LE, src_data, src_stride, src_width,
                          AV_PIX_FMT_BGRA1010102LE, dst_data, dst_stride, dst_width, SWS_POINT));

  for (int i = 0; i < src_width; ++i) {
    EXPECT_EQ(pixel_at(dst, i * 2) & 0x3, static_cast<std::uint32_t>(i)) << "pixel " << i;
    EXPECT_EQ(pixel_at(dst, i * 2 + 1) & 0x3, static_cast<std::uint32_t>(i)) << "pixel " << i;
  }
}

/**
 * @brief The RGBA and BGRA writers must place the channels at opposite ends.
 */
TEST_F(HdrAlpha, ChannelOrder) {
  constexpr int width = 4;
  std::uint32_t src[width];
  for (int i = 0; i < width; ++i) {
    src[i] = pack_bgra1010102(1023, 0, 0, 3);
  }

  const std::uint8_t *src_data[4] = { reinterpret_cast<const std::uint8_t *>(src), nullptr, nullptr, nullptr };
  const int src_stride[4] = { width * 4, 0, 0, 0 };

  for (auto dst_format : { AV_PIX_FMT_BGRA1010102LE, AV_PIX_FMT_RGBA1010102LE }) {
    std::uint8_t dst[width * 4] = {};
    std::uint8_t *dst_data[4] = { dst, nullptr, nullptr, nullptr };
    const int dst_stride[4] = { width * 4, 0, 0, 0 };

    ASSERT_TRUE(convert_row(AV_PIX_FMT_BGRA1010102LE, src_data, src_stride, width,
                            dst_format, dst_data, dst_stride, width, SWS_POINT));

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
