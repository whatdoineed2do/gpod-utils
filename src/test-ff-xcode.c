#include <sys/types.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "gpod-ffmpeg.h"
#include "test-ff-wav.h"


#define TEST_FF_XCODE_WAV_SAMPLE "test-ff-xcode.wav"
int _generate_sample()
{
    int  fd;
    if ( (fd=open(TEST_FF_XCODE_WAV_SAMPLE, O_CREAT | O_TRUNC | O_WRONLY, 0600)) < 0) {
        return -errno;
    }

    if (write(fd, sample_wav, sizeof(sample_wav)) != sizeof(sample_wav)) {
        return 1;
    }
    close(fd);
    return 0;
}


int main()
{
    if (_generate_sample() != 0) {
        fprintf(stderr, "unabled to generated data sample %s - %s\n", TEST_FF_XCODE_WAV_SAMPLE, strerror(errno));
        return -1;
    }


    struct gpod_ff_transcode_ctx  xfrm;

    const struct Fmt {
        enum AVCodecID  codec_id;
        enum gpod_ff_transcode_quality  quality;
        const char*  name;
    } fmts[] = {
/* as per ffmpeg's aac_encode_init() when
 *   ffmpeg -i foo.wav -c:a aac -q:a 2 foo.m4a
   {
      codec_type = AVMEDIA_TYPE_AUDIO,
      codec = 0x16b1360 <ff_aac_encoder>,
      codec_id = AV_CODEC_ID_AAC,
      codec_tag = 0,
      bit_rate = 0,
      global_quality = 236,
      compression_level = -1,
      flags = 4194306,
      time_base = {
	num = 1,
	den = 44100
      },
      ticks_per_frame = 1,
      gop_size = 0,
      get_format = 0x8575b0 <avcodec_default_get_format>,
      mpeg_quant = 0,
      i_quant_factor = 0,
      i_quant_offset = 0,
      lumi_masking = 0,
      temporal_cplx_masking = 0,
      spatial_cplx_masking = 0,
      p_masking = 0,
      dark_masking = 0,
      slice_count = 0,
      prediction_method = 0,
    --Type <RET> for more, q to quit, c to continue without paging--
      slice_offset = 0x0,
      sample_aspect_ratio = {
	num = 0,
	den = 1
      },
      me_cmp = 0,
      me_sub_cmp = 0,
      mb_cmp = 0,
      ildct_cmp = 0,
      dia_size = 0,
      last_predictor_count = 0,
      pre_me = 0,
      me_pre_cmp = 0,
      pre_dia_size = 0,
      me_subpel_quality = 0,
      me_range = 0,
      slice_flags = 0,
      mb_decision = 0,
      intra_matrix = 0x0,
      inter_matrix = 0x0,
      scenechange_threshold = 0,
      noise_reduction = 0,
      intra_dc_precision = 0,
      skip_top = 0,
      skip_bottom = 0,
      mb_lmin = 0,
      mb_lmax = 0,
      me_penalty_compensation = 0,
      bidir_refine = 0,
      brd_scale = 0,
      keyint_min = 0,
      refs = 0,
      chromaoffset = 0,
      mv0_threshold = 0,
      b_sensitivity = 0,
      color_primaries = AVCOL_PRI_RESERVED0,
      color_trc = AVCOL_TRC_RESERVED0,
      colorspace = AVCOL_SPC_RGB,
      color_range = AVCOL_RANGE_UNSPECIFIED,
      chroma_sample_location = AVCHROMA_LOC_UNSPECIFIED,
      sample_rate = 44100,
      channels = 2,
      sample_fmt = AV_SAMPLE_FMT_FLTP,
      frame_size = 0,
      frame_number = 0,
      channel_layout = 3,
      request_channel_layout = 0,
      request_sample_fmt = AV_SAMPLE_FMT_NONE,
      qcompress = 0,
      qblur = 0,
      qmin = 0,
      qmax = 0,
      max_qdiff = 0,
      rc_buffer_size = 0,
      rc_override_count = 0,
      rc_override = 0x0,
      rc_max_rate = 0,
      rc_min_rate = 0,
      rc_max_available_vbv_use = 0,
      rc_min_vbv_overflow_use = 0,
      rc_initial_buffer_occupancy = 0,
      coder_type = 0,
      context_model = 0,
      frame_skip_threshold = 0,
      frame_skip_factor = 0,
      frame_skip_exp = 0,
      frame_skip_cmp = 0,
      trellis = 0,
      min_prediction_order = -1,
      max_prediction_order = -1,
      timecode_frame_start = 0,
      rtp_callback = 0x0,
      rtp_payload_size = 0,
      mv_bits = 0,
      header_bits = 0,
      i_tex_bits = 0,
      p_tex_bits = 0,
      i_count = 0,
      p_count = 0,
      skip_count = 0,
      misc_bits = 0,
      frame_bits = 0,
      stats_out = 0x0,
      stats_in = 0x0,
      workaround_bugs = 0,
      strict_std_compliance = 0,
      error_concealment = 0,
    --Type <RET> for more, q to quit, c to continue without paging--
      debug = 0,
      err_recognition = 0,
      reordered_opaque = -9223372036854775808,
      hwaccel = 0x0,
      hwaccel_context = 0x0,
      error = {0, 0, 0, 0, 0, 0, 0, 0},
      dct_algo = 0,
      idct_algo = 0,
      bits_per_coded_sample = 0,
      bits_per_raw_sample = 0,
      lowres = 0,
      coded_frame = 0x20b0000,
      thread_count = 1,
      thread_type = 3,
      active_thread_type = 0,
      thread_safe_callbacks = 0,
      execute = 0xb84ee0 <avcodec_default_execute>,
      execute2 = 0xb84f60 <avcodec_default_execute2>,
      nsse_weight = 0,
      profile = -99,
      level = -99,
      skip_loop_filter = AVDISCARD_DEFAULT,
      skip_idct = AVDISCARD_DEFAULT,
      skip_frame = AVDISCARD_DEFAULT,
      subtitle_header = 0x0,
      subtitle_header_size = 0,
      vbv_delay = 0,
      side_data_only_packets = 1,
      initial_padding = 0,
      framerate = {
	num = 0,
	den = 1
      },
      sw_pix_fmt = AV_PIX_FMT_NONE,
      pkt_timebase = {
	num = 0,
	den = 1
      },
      codec_descriptor = 0x12b4ee0 <codec_descriptors+16416>,
      pts_correction_num_faulty_pts = 0,
      pts_correction_num_faulty_dts = 0,
      pts_correction_last_pts = -9223372036854775808,
      pts_correction_last_dts = -9223372036854775808,
      sub_charenc = 0x0,
      sub_charenc_mode = 0,
      skip_alpha = 0,
      seek_preroll = 0,
    --Type <RET> for more, q to quit, c to continue without paging--
      debug_mv = 0,
      chroma_intra_matrix = 0x0,
      dump_separator = 0x0,
      codec_whitelist = 0x0,
      properties = 0,
      coded_side_data = 0x0,
      nb_coded_side_data = 0,
      hw_frames_ctx = 0x0,
      sub_text_format = 0,
      trailing_padding = 0,
      max_pixels = 2147483647,
      hw_device_ctx = 0x0,
      hwaccel_flags = 0,
      apply_cropping = 0,
      extra_hw_frames = 0,
      discard_damaged_percentage = 0,
      max_samples = 2147483647,
      export_side_data = 0
   }
 */
	//channels=2 channel_layout=3 bit_rate=0 flags=2 global_quality=118 compression_level=4294967295 codec_tag=0 sample_rate=44100 qcompress=0 request channel_layout=4294967295 request_sample_fmt=4204882
        { AV_CODEC_ID_AAC, GPOD_FF_XCODE_VBR1,   "test-ff-aac-vbr1.aac" },
	//channels=2 channel_layout=3 bit_rate=0 flags=4194306 global_quality=118 compression_level=4294967295 codec_tag=0 sample_rate=44100 qcompress=0 request channel_layout=4294967295 request_sample_fmt=4204882
        { AV_CODEC_ID_AAC, GPOD_FF_XCODE_VBR1,   "test-ff-aac-vbr1.m4a" },

	//channels=2 channel_layout=3 bit_rate=256000 flags=4194304 global_quality=0 compression_level=4294967295 codec_tag=0 sample_rate=44100 qcompress=0 request channel_layout=4294967295 request_sample_fmt=4204882
        { AV_CODEC_ID_AAC, GPOD_FF_XCODE_CBR256, "test-ff-aac-cbr256.m4a" },
	//channels=2 channel_layout=3 bit_rate=160000 flags=4194304 global_quality=0 compression_level=4294967295 codec_tag=0 sample_rate=44100 qcompress=0 request channel_layout=4294967295 request_sample_fmt=4204882
        { AV_CODEC_ID_AAC, GPOD_FF_XCODE_CBR160, "test-ff-aac-cbr160.m4a" },

	//channels=2 channel_layout=3 bit_rate=0 flags=2 global_quality=118 compression_level=4294967295 codec_tag=0 sample_rate=44100 qcompress=0 request channel_layout=4294967295 request_sample_fmt=4204882
        { AV_CODEC_ID_MP3, GPOD_FF_XCODE_VBR1,   "test-ff-mp3-vbr1.mp3" },
	//channels=2 channel_layout=3 bit_rate=160000 flags=0 global_quality=0 compression_level=4294967295 codec_tag=0 sample_rate=44100 qcompress=0 request channel_layout=4294967295 request_sample_fmt=4204882
        { AV_CODEC_ID_MP3, GPOD_FF_XCODE_CBR160, "test-ff-mp3-cbr160.mp3" },

        { 0, 0, NULL }
    };

    struct gpod_ff_media_info  mi;
    gpod_ff_media_info_init(&mi);
    strcpy(mi.path, TEST_FF_XCODE_WAV_SAMPLE);

    struct gpod_ff_transcode_ctx  xcode;
    char*  err;

    const struct Fmt*  p = fmts;
    while (p->name)
    {
        err = NULL;
        gpod_ff_transcode_ctx_init(&xcode, p->codec_id == AV_CODEC_ID_MP3, p->quality);
        strcpy(xcode.path, p->name);
        xcode.audio_opts.samplerate = 44100;

        ++p;

        printf("xcoding %s.. ", xcode.path);
        fflush(stdout);
        if (gpod_ff_transcode(&mi, &xcode, &err) < 0) {
            if (err) {
                fprintf(stderr, "failed xcode '%s' - %s\n", xcode.path, err);
                free(err);
            }
        }
    }


    return 0;
}
