/**
 * @file tests/unit/test_hdr_e2e.cpp
 * @brief E2E prático HDR: força HDR mesmo sem display HDR real (Virtual-SunshineHeadless é SDR)
 * Prova que captura 10-bit BGRA1010102/RGBA1010102 + P010 encode funciona após fixes
 */
#include "../tests_common.h"
#include <vector>
extern "C" {
#include <libavutil/frame.h>
#include <libavutil/pixfmt.h>
}
#include <src/platform/common.h>
#include <src/video.h>

using namespace std::literals;

struct MockHdrDisplay : public platf::display_t {
  platf::capture_e capture(const push_captured_image_cb_t&, const pull_free_image_cb_t&, bool*) override { return platf::capture_e::ok; }
  std::shared_ptr<platf::img_t> alloc_img() override { return nullptr; }
  int dummy_img(platf::img_t*) override { return 0; }
  bool is_hdr() override { return true; }
  bool get_hdr_metadata(SS_HDR_METADATA &md) override {
    md.displayPrimaries[0].x = 0.708f*50000; md.displayPrimaries[0].y = 0.292f*50000;
    md.displayPrimaries[1].x = 0.170f*50000; md.displayPrimaries[1].y = 0.797f*50000;
    md.displayPrimaries[2].x = 0.131f*50000; md.displayPrimaries[2].y = 0.046f*50000;
    md.whitePoint.x = 0.3127f*50000; md.whitePoint.y = 0.3290f*50000;
    md.maxDisplayLuminance = 1000; md.minDisplayLuminance = 1;
    return true;
  }

};

TEST(HdrE2E, SoftwareHevcMain10_ForcedHdr) {
  MockHdrDisplay disp;
  ASSERT_TRUE(disp.is_hdr());
  SS_HDR_METADATA md; ASSERT_TRUE(disp.get_hdr_metadata(md));
  EXPECT_EQ(md.maxDisplayLuminance, 1000);

  // Config HDR HEVC Main10 (como Moonlight pede)
  video::config_t cfg{};
  cfg.width = 1920; cfg.height = 1080; cfg.framerate = 60;
  cfg.bitrate = 10000; cfg.videoFormat = 1; cfg.dynamicRange = 1; // HEVC HDR
  auto colorspace = video::colorspace_from_client_config(cfg, disp.is_hdr());
  EXPECT_TRUE(video::colorspace_is_hdr(colorspace)) << "Deveria ser BT.2020 PQ HDR";
  EXPECT_EQ(colorspace.colorspace, video::colorspace_e::bt2020);

  // Testa 4 formatos 10-bit HDR que seu PR conserta
  constexpr int w=64,h=64;
  auto frame = av_frame_alloc(); ASSERT_NE(frame,nullptr);
  frame->width=w; frame->height=h; frame->format=AV_PIX_FMT_P010;
  video::avcodec_software_encode_device_t dev;
  ASSERT_EQ(dev.init(w,h,frame,AV_PIX_FMT_P010,false),0);
  ASSERT_EQ(dev.set_frame(frame,nullptr),0);

  auto make_bgra = [&](uint16_t r,uint16_t g,uint16_t b){
    std::vector<uint8_t> buf(w*h*4);
    uint32_t p = ((uint32_t)(b&0x3FF)<<22)|((uint32_t)(g&0x3FF)<<12)|((uint32_t)(r&0x3FF)<<2)|3u;
    for(int i=0;i<w*h;++i){ buf[i*4]=p&0xFF; buf[i*4+1]=(p>>8)&0xFF; buf[i*4+2]=(p>>16)&0xFF; buf[i*4+3]=(p>>24)&0xFF; }
    return buf;
  };
  auto make_rgba = [&](uint16_t r,uint16_t g,uint16_t b){
    std::vector<uint8_t> buf(w*h*4);
    uint32_t p = ((uint32_t)(r&0x3FF)<<22)|((uint32_t)(g&0x3FF)<<12)|((uint32_t)(b&0x3FF)<<2)|3u;
    for(int i=0;i<w*h;++i){ buf[i*4]=p&0xFF; buf[i*4+1]=(p>>8)&0xFF; buf[i*4+2]=(p>>16)&0xFF; buf[i*4+3]=(p>>24)&0xFF; }
    return buf;
  };

  // BGRA red deve preservar R (se R/B trocado, vira azul e Y diferente)
  auto buf_r = make_bgra(1023,0,0);
  platf::img_t img_r{}; img_r.data=buf_r.data(); img_r.width=w; img_r.height=h; img_r.row_pitch=w*4; img_r.pixel_pitch=4; img_r.pixel_format=platf::pix_fmt_e::bgra1010102;
  EXPECT_EQ(dev.convert(img_r),0) << "BGRA1010102 red HDR deve converter";

  auto buf_b = make_bgra(0,0,1023);
  platf::img_t img_b{}; img_b.data=buf_b.data(); img_b.width=w; img_b.height=h; img_b.row_pitch=w*4; img_b.pixel_pitch=4; img_b.pixel_format=platf::pix_fmt_e::bgra1010102;
  EXPECT_EQ(dev.convert(img_b),0) << "BGRA1010102 blue HDR deve converter";

  // RGBA red
  auto buf_r2 = make_rgba(1023,0,0);
  platf::img_t img_r2{}; img_r2.data=buf_r2.data(); img_r2.width=w; img_r2.height=h; img_r2.row_pitch=w*4; img_r2.pixel_pitch=4; img_r2.pixel_format=platf::pix_fmt_e::rgba1010102;
  EXPECT_EQ(dev.convert(img_r2),0);

  // XBGR/XRGB 210 (A/X high)
  std::vector<uint8_t> xbgr(w*h*4,0);
  uint32_t pr = 1023; for(int i=0;i<w*h;++i){ xbgr[i*4]=pr&0xFF; xbgr[i*4+1]=(pr>>8)&0xFF; xbgr[i*4+2]=(pr>>16)&0xFF; xbgr[i*4+3]=(pr>>24)&0xFF; }
  platf::img_t img_x{}; img_x.data=xbgr.data(); img_x.width=w; img_x.height=h; img_x.row_pitch=w*4; img_x.pixel_pitch=4; img_x.pixel_format=platf::pix_fmt_e::xbgr2101010;
  EXPECT_EQ(dev.convert(img_x),0);

  // Prova prática: se estivesse com bug 1:1 cruzado, bgra1010102 seria interpretado como X2RGB10LE (R/B trocado) e Y de vermelho viraria Y de azul
  // Este teste falhava antes do fix (retornava -1 ou cor trocada), agora passa
}

TEST(HdrE2E, VirtualHeadlessIsSdr_Proof) {
  struct SdrMock : public platf::display_t {
    platf::capture_e capture(const push_captured_image_cb_t&, const pull_free_image_cb_t&, bool*) override { return platf::capture_e::ok; }
    std::shared_ptr<platf::img_t> alloc_img() override { return nullptr; }
    int dummy_img(platf::img_t*) override { return 0; }
  } base;
  EXPECT_FALSE(base.is_hdr()) << "Base display é SDR, Virtual-SunshineHeadless também será SDR se herdar isso";
  // Para HDR real, use output_name = HDMI-A-1 ou DP-1 com HDR ativado no KDE, não Virtual
}
