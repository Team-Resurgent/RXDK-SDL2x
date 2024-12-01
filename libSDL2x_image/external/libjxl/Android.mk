LOCAL_PATH:= $(call my-dir)

include $(CLEAR_VARS)

LOCAL_C_INCLUDES := \
    $(LOCAL_PATH)/android \
    $(LOCAL_PATH)/lib/include \
    $(LOCAL_PATH)/third_party/highway \
    $(LOCAL_PATH)/third_party/brotli/c/include \
    $(LOCAL_PATH)/third_party/brotli/c/dec \

BROTLI_SRC_FILES := \
    $(LOCAL_PATH)/third_party/brotli/c/common/dictionary.c \
    $(LOCAL_PATH)/third_party/brotli/c/common/transform.c \
    $(LOCAL_PATH)/third_party/brotli/c/dec/bit_reader.c \
    $(LOCAL_PATH)/third_party/brotli/c/dec/decode.c \
    $(LOCAL_PATH)/third_party/brotli/c/dec/huffman.c \
    $(LOCAL_PATH)/third_party/brotli/c/dec/state.c \

HIGHWAY_SRC_FILES := \
    $(LOCAL_PATH)/third_party/highway/hwy/aligned_allocator.cc \
    $(LOCAL_PATH)/third_party/highway/hwy/targets.cc \

LOCAL_SRC_FILES :=  \
    $(BROTLI_SRC_FILES) \
    $(HIGHWAY_SRC_FILES) \
    lib/jxl/ac_strategy.cc \
    lib/jxl/alpha.cc \
    lib/jxl/ans_common.cc \
    lib/jxl/aux_out.cc \
    lib/jxl/base/cache_aligned.cc \
    lib/jxl/base/data_parallel.cc \
    lib/jxl/base/descriptive_statistics.cc \
    lib/jxl/base/padded_bytes.cc \
    lib/jxl/base/status.cc \
    lib/jxl/blending.cc \
    lib/jxl/chroma_from_luma.cc \
    lib/jxl/coeff_order.cc \
    lib/jxl/color_encoding_internal.cc \
    lib/jxl/color_management.cc \
    lib/jxl/compressed_dc.cc \
    lib/jxl/convolve.cc \
    lib/jxl/dct_scales.cc \
    lib/jxl/dec_ans.cc \
    lib/jxl/dec_cache.cc \
    lib/jxl/dec_context_map.cc \
    lib/jxl/dec_external_image.cc \
    lib/jxl/dec_frame.cc \
    lib/jxl/dec_group.cc \
    lib/jxl/dec_group_border.cc \
    lib/jxl/dec_huffman.cc \
    lib/jxl/dec_modular.cc \
    lib/jxl/dec_noise.cc \
    lib/jxl/dec_patch_dictionary.cc \
    lib/jxl/dec_reconstruct.cc \
    lib/jxl/dec_upsample.cc \
    lib/jxl/dec_xyb.cc \
    lib/jxl/decode.cc \
    lib/jxl/decode_to_jpeg.cc \
    lib/jxl/enc_bit_writer.cc \
    lib/jxl/entropy_coder.cc \
    lib/jxl/epf.cc \
    lib/jxl/fields.cc \
    lib/jxl/filters.cc \
    lib/jxl/frame_header.cc \
    lib/jxl/gauss_blur.cc \
    lib/jxl/headers.cc \
    lib/jxl/huffman_table.cc \
    lib/jxl/icc_codec.cc \
    lib/jxl/icc_codec_common.cc \
    lib/jxl/image.cc \
    lib/jxl/image_bundle.cc \
    lib/jxl/image_metadata.cc \
    lib/jxl/jpeg/dec_jpeg_data.cc \
    lib/jxl/jpeg/dec_jpeg_data_writer.cc \
    lib/jxl/jpeg/jpeg_data.cc \
    lib/jxl/loop_filter.cc \
    lib/jxl/luminance.cc \
    lib/jxl/memory_manager_internal.cc \
    lib/jxl/modular/encoding/dec_ma.cc \
    lib/jxl/modular/encoding/encoding.cc \
    lib/jxl/modular/modular_image.cc \
    lib/jxl/modular/transform/squeeze.cc \
    lib/jxl/modular/transform/transform.cc \
    lib/jxl/opsin_params.cc \
    lib/jxl/passes_state.cc \
    lib/jxl/quant_weights.cc \
    lib/jxl/quantizer.cc \
    lib/jxl/splines.cc \
    lib/jxl/toc.cc \

LOCAL_CFLAGS := -DJXL_INTERNAL_LIBRARY_BUILD -DJPEGXL_MAJOR_VERSION=0 -DJPEGXL_MINOR_VERSION=6 -DJPEGXL_PATCH_VERSION=1 -DJPEGXL_ENABLE_TRANSCODE_JPEG

LOCAL_MODULE := jxl

include $(BUILD_STATIC_LIBRARY)
