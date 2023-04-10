#ifndef CODEC_H
#define CODEC_H


#include "log.h"
#include <cstdint>
#include <string>
#include <vector>

class Buffer;

/**
 * The video codec id.
 * @doc video_file_format_spec_v10_1.pdf, page78, E.4.3.1 VIDEODATA
 * CodecID UB [4]
 * Codec Identifier. The following values are defined for FLV:
 *      2 = Sorenson H.263
 *      3 = Screen video
 *      4 = On2 VP6
 *      5 = On2 VP6 with alpha channel
 *      6 = Screen video version 2
 *      7 = AVC
 */
enum VideoCodecId
{
    // set to the zero to reserved, for array map.
    VideoCodecIdReserved = 0,
    VideoCodecIdForbidden = 0,
    VideoCodecIdReserved1 = 1,
    VideoCodecIdReserved2 = 9,

    // for user to disable video, for example, use pure audio hls.
    VideoCodecIdDisabled = 8,

    VideoCodecIdSorensonH263 = 2,
    VideoCodecIdScreenVideo = 3,
    VideoCodecIdOn2VP6 = 4,
    VideoCodecIdOn2VP6WithAlphaChannel = 5,
    VideoCodecIdScreenVideoVersion2 = 6,
    VideoCodecIdAVC = 7,
    // See page 79 at @doc https://github.com/CDN-Union/H265/blob/master/Document/video_file_format_spec_v10_1_ksyun_20170615.doc
    VideoCodecIdHEVC = 12,
    // https://mp.weixin.qq.com/s/H3qI7zsON5sdf4oDJ9qlkg
    VideoCodecIdAV1 = 13,
};
std::string VideoCodecId2str(VideoCodecId codec);

/**
 * The video AVC frame trait(characteristic).
 * @doc video_file_format_spec_v10_1.pdf, page79, E.4.3.2 AVCVIDEOPACKET
 * AVCPacketType IF CodecID == 7 UI8
 * The following values are defined:
 *      0 = AVC sequence header
 *      1 = AVC NALU
 *      2 = AVC end of sequence (lower level NALU sequence ender is not required or supported)
 */
enum VideoAvcFrameTrait
{
    // set to the max value to reserved, for array map.
    VideoAvcFrameTraitReserved = 3,
    VideoAvcFrameTraitForbidden = 3,

    VideoAvcFrameTraitSequenceHeader = 0,
    VideoAvcFrameTraitNALU = 1,
    VideoAvcFrameTraitSequenceHeaderEOF = 2,
};

/**
 * The video AVC frame type, such as I/P/B.
 * @doc video_file_format_spec_v10_1.pdf, page78, E.4.3.1 VIDEODATA
 * Frame Type UB [4]
 * Type of video frame. The following values are defined:
 *      1 = key frame (for AVC, a seekable frame)
 *      2 = inter frame (for AVC, a non-seekable frame)
 *      3 = disposable inter frame (H.263 only)
 *      4 = generated key frame (reserved for server use only)
 *      5 = video info/command frame
 */
enum VideoAvcFrameType
{
    // set to the zero to reserved, for array map.
    VideoAvcFrameTypeReserved = 0,
    VideoAvcFrameTypeForbidden = 0,
    VideoAvcFrameTypeReserved1 = 6,

    VideoAvcFrameTypeKeyFrame = 1,
    VideoAvcFrameTypeInterFrame = 2,
    VideoAvcFrameTypeDisposableInterFrame = 3,
    VideoAvcFrameTypeGeneratedKeyFrame = 4,
    VideoAvcFrameTypeVideoInfoFrame = 5,
};

/**
 * The audio codec id.
 * @doc video_file_format_spec_v10_1.pdf, page 76, E.4.2 Audio Tags
 * SoundFormat UB [4]
 * Format of SoundData. The following values are defined:
 *     0 = Linear PCM, platform endian
 *     1 = ADPCM
 *     2 = MP3
 *     3 = Linear PCM, little endian
 *     4 = Nellymoser 16 kHz mono
 *     5 = Nellymoser 8 kHz mono
 *     6 = Nellymoser
 *     7 = G.711 A-law logarithmic PCM
 *     8 = G.711 mu-law logarithmic PCM
 *     9 = reserved
 *     10 = AAC
 *     11 = Speex
 *     14 = MP3 8 kHz
 *     15 = Device-specific sound
 * Formats 7, 8, 14, and 15 are reserved.
 * AAC is supported in Flash Player 9,0,115,0 and higher.
 * Speex is supported in Flash Player 10 and higher.
 */
enum AudioCodecId
{
    // set to the max value to reserved, for array map.
    AudioCodecIdReserved1 = 16,
    AudioCodecIdForbidden = 16,

    // for user to disable audio, for example, use pure video hls.
    AudioCodecIdDisabled = 17,

    AudioCodecIdLinearPCMPlatformEndian = 0,
    AudioCodecIdADPCM = 1,
    AudioCodecIdMP3 = 2,
    AudioCodecIdLinearPCMLittleEndian = 3,
    AudioCodecIdNellymoser16kHzMono = 4,
    AudioCodecIdNellymoser8kHzMono = 5,
    AudioCodecIdNellymoser = 6,
    AudioCodecIdReservedG711AlawLogarithmicPCM = 7,
    AudioCodecIdReservedG711MuLawLogarithmicPCM = 8,
    AudioCodecIdReserved = 9,
    AudioCodecIdAAC = 10,
    AudioCodecIdSpeex = 11,
    // For FLV, it's undefined, we define it as Opus for WebRTC.
    AudioCodecIdOpus = 13,
    AudioCodecIdReservedMP3_8kHz = 14,
    AudioCodecIdReservedDeviceSpecificSound = 15,
};
std::string AudioCodecId2str(AudioCodecId codec);

/**
 * The audio AAC frame trait(characteristic).
 * @doc video_file_format_spec_v10_1.pdf, page 77, E.4.2 Audio Tags
 * AACPacketType IF SoundFormat == 10 or 13 UI8
 * The following values are defined:
 *      0 = AAC sequence header
 *      1 = AAC raw
 */
enum AudioAacFrameTrait
{
    // set to the max value to reserved, for array map.
    AudioAacFrameTraitReserved = 0xff,
    AudioAacFrameTraitForbidden = 0xff,

    AudioAacFrameTraitSequenceHeader = 0,
    AudioAacFrameTraitRawData = 1,

    // For Opus, the frame trait, may has more than one traits.
    AudioOpusFrameTraitRaw = 2,
    AudioOpusFrameTraitSamplingRate = 4,
    AudioOpusFrameTraitAudioLevel = 8,

    // 16/32 reserved for g711a/g711u

    // For MP3
    AudioMp3FrameTrait = 64,
};

/**
 * The audio sample rate.
 * @see srs_flv_srates and srs_aac_srates.
 * @doc video_file_format_spec_v10_1.pdf, page 76, E.4.2 Audio Tags
 *      0 = 5.5 kHz = 5512 Hz
 *      1 = 11 kHz = 11025 Hz
 *      2 = 22 kHz = 22050 Hz
 *      3 = 44 kHz = 44100 Hz
 * However, we can extends this table.
 * @remark Use srs_flv_srates to convert it.
 */
enum AudioSampleRate
{
    // set to the max value to reserved, for array map.
    AudioSampleRateReserved = 0xff,
    AudioSampleRateForbidden = 0xff,

    // For FLV, only support 5, 11, 22, 44KHz sampling rate.
    AudioSampleRate5512 = 0,
    AudioSampleRate11025 = 1,
    AudioSampleRate22050 = 2,
    AudioSampleRate44100 = 3,

    // For Opus, support 8, 12, 16, 24, 48KHz
    // We will write a UINT8 sampling rate after FLV audio tag header.
    // @doc https://tools.ietf.org/html/rfc6716#section-2
    AudioSampleRateNB8kHz   = 8,  // NB (narrowband)
    AudioSampleRateMB12kHz  = 12, // MB (medium-band)
    AudioSampleRateWB16kHz  = 16, // WB (wideband)
    AudioSampleRateSWB24kHz = 24, // SWB (super-wideband)
    AudioSampleRateFB48kHz  = 48, // FB (fullband)
};
std::string AudioSampleRate2str(AudioSampleRate v);

/**
 * The frame type, for example, audio, video or data.
 * @doc video_file_format_spec_v10_1.pdf, page 75, E.4.1 FLV Tag
 */
enum FrameType
{
    // set to the zero to reserved, for array map.
    FrameTypeReserved = 0,
    FrameTypeForbidden = 0,

    // 8 = audio
    FrameTypeAudio = 8,
    // 9 = video
    FrameTypeVideo = 9,
    // 18 = script data
    FrameTypeScript = 18,
};

/**
 * Fast tough the codec of FLV video.
 * @doc video_file_format_spec_v10_1.pdf, page 78, E.4.3 Video Tags
 */
class FlvVideo
{
public:
    FlvVideo();
    virtual ~FlvVideo();
    // the following function used to finger out the flv/rtmp packet detail.
public:
    /**
     * only check the frame_type, not check the codec type.
     */
    static bool Keyframe(char* data, int size);
    /**
     * check codec h264, keyframe, sequence header
     */
    // TODO: FIXME: Remove it, use SrsFormat instead.
    static bool Sh(char* data, int size);
    /**
     * check codec h264.
     */
    static bool H264(char* data, int size);
    /**
     * check the video RTMP/flv header info,
     * @return true if video RTMP/flv header is ok.
     * @remark all type of audio is possible, no need to check audio.
     */
    static bool Acceptable(char* data, int size);
};

/**
 * Fast tough the codec of FLV video.
 * @doc video_file_format_spec_v10_1.pdf, page 76, E.4.2 Audio Tags
 */
class FlvAudio
{
public:
    FlvAudio();
    virtual ~FlvAudio();
    // the following function used to finger out the flv/rtmp packet detail.
public:
    /**
     * check codec aac, sequence header
     */
    static bool Sh(char* data, int size);
    /**
     * check codec aac.
     */
    static bool Aac(char* data, int size);
};

/**
 * the public data, event HLS disable, others can use it.
 */
/**
 * the flv sample rate map
 */
extern int flv_srates[];

/**
 * the aac sample rate map
 */
extern int aac_srates[];

// The number of aac samplerates, size for srs_aac_srates.
#define AAcSampleRateNumbers 16

// The impossible aac sample rate index.
#define AacSampleRateUnset 15

// The max number of NALUs in a video, or aac frame in audio packet.
#define MaxNbSamples 256

/**
 * The audio sample size in bits.
 * @doc video_file_format_spec_v10_1.pdf, page 76, E.4.2 Audio Tags
 * Size of each audio sample. This parameter only pertains to
 * uncompressed formats. Compressed formats always decode
 * to 16 bits internally.
 *      0 = 8-bit samples
 *      1 = 16-bit samples
 */
enum AudioSampleBits
{
    // set to the max value to reserved, for array map.
    AudioSampleBitsReserved = 2,
    AudioSampleBitsForbidden = 2,

    AudioSampleBits8bit = 0,
    AudioSampleBits16bit = 1,
};
std::string AudioSampleBits2str(AudioSampleBits v);

/**
 * The audio channels.
 * @doc video_file_format_spec_v10_1.pdf, page 77, E.4.2 Audio Tags
 * Mono or stereo sound
 *      0 = Mono sound
 *      1 = Stereo sound
 */
enum AudioChannels
{
    // set to the max value to reserved, for array map.
    AudioChannelsReserved = 2,
    AudioChannelsForbidden = 2,

    AudioChannelsMono = 0,
    AudioChannelsStereo = 1,
};
std::string AudioChannels2str(AudioChannels v);

/**
 * Table 7-1 - NAL unit type codes, syntax element categories, and NAL unit type classes
 * ISO_IEC_14496-10-AVC-2012.pdf, page 83.
 */
enum AvcNaluType
{
    // Unspecified
    AvcNaluTypeReserved = 0,
    AvcNaluTypeForbidden = 0,

    // Coded slice of a non-IDR picture slice_layer_without_partitioning_rbsp( )
    AvcNaluTypeNonIDR = 1,
    // Coded slice data partition A slice_data_partition_a_layer_rbsp( )
    AvcNaluTypeDataPartitionA = 2,
    // Coded slice data partition B slice_data_partition_b_layer_rbsp( )
    AvcNaluTypeDataPartitionB = 3,
    // Coded slice data partition C slice_data_partition_c_layer_rbsp( )
    AvcNaluTypeDataPartitionC = 4,
    // Coded slice of an IDR picture slice_layer_without_partitioning_rbsp( )
    AvcNaluTypeIDR = 5,
    // Supplemental enhancement information (SEI) sei_rbsp( )
    AvcNaluTypeSEI = 6,
    // Sequence parameter set seq_parameter_set_rbsp( )
    AvcNaluTypeSPS = 7,
    // Picture parameter set pic_parameter_set_rbsp( )
    AvcNaluTypePPS = 8,
    // Access unit delimiter access_unit_delimiter_rbsp( )
    AvcNaluTypeAccessUnitDelimiter = 9,
    // End of sequence end_of_seq_rbsp( )
    AvcNaluTypeEOSequence = 10,
    // End of stream end_of_stream_rbsp( )
    AvcNaluTypeEOStream = 11,
    // Filler data filler_data_rbsp( )
    AvcNaluTypeFilterData = 12,
    // Sequence parameter set extension seq_parameter_set_extension_rbsp( )
    AvcNaluTypeSPSExt = 13,
    // Prefix NAL unit prefix_nal_unit_rbsp( )
    AvcNaluTypePrefixNALU = 14,
    // Subset sequence parameter set subset_seq_parameter_set_rbsp( )
    AvcNaluTypeSubsetSPS = 15,
    // Coded slice of an auxiliary coded picture without partitioning slice_layer_without_partitioning_rbsp( )
    AvcNaluTypeLayerWithoutPartition = 19,
    // Coded slice extension slice_layer_extension_rbsp( )
    AvcNaluTypeCodedSliceExt = 20,
};
std::string AvcNalu2str(AvcNaluType nalu_type);

/**
 * Table 7-6 – Name association to slice_type
 * ISO_IEC_14496-10-AVC-2012.pdf, page 105.
 */
enum AvcSliceType
{
    AvcSliceTypeP   = 0,
    AvcSliceTypeB   = 1,
    AvcSliceTypeI   = 2,
    AvcSliceTypeSP  = 3,
    AvcSliceTypeSI  = 4,
    AvcSliceTypeP1  = 5,
    AvcSliceTypeB1  = 6,
    AvcSliceTypeI1  = 7,
    AvcSliceTypeSP1 = 8,
    AvcSliceTypeSI1 = 9,
};

/**
 * the avc payload format, must be ibmf or annexb format.
 * we guess by annexb first, then ibmf for the first time,
 * and we always use the guessed format for the next time.
 */
enum AvcPayloadFormat
{
    AvcPayloadFormatGuess = 0,
    AvcPayloadFormatAnnexb,
    AvcPayloadFormatIbmf,
};

/**
 * the aac profile, for ADTS(HLS/TS)
 * @see https://github.com/ossrs/srs/issues/310
 */
enum AacProfile
{
    AacProfileReserved = 3,

    // @see 7.1 Profiles, ISO_IEC_13818-7-AAC-2004.pdf, page 40
    AacProfileMain = 0,
    AacProfileLC = 1,
    AacProfileSSR = 2,
};
std::string AacProfile2str(AacProfile aac_profile);

/**
 * the aac object type, for RTMP sequence header
 * for AudioSpecificConfig, @see ISO_IEC_14496-3-AAC-2001.pdf, page 33
 * for audioObjectType, @see ISO_IEC_14496-3-AAC-2001.pdf, page 23
 */
enum AacObjectType
{
    AacObjectTypeReserved = 0,
    AacObjectTypeForbidden = 0,

    // Table 1.1 - Audio Object Type definition
    // @see @see ISO_IEC_14496-3-AAC-2001.pdf, page 23
    AacObjectTypeAacMain = 1,
    AacObjectTypeAacLC = 2,
    AacObjectTypeAacSSR = 3,

    // AAC HE = LC+SBR
    AacObjectTypeAacHE = 5,
    // AAC HEv2 = LC+SBR+PS
    AacObjectTypeAacHEV2 = 29,
};
std::string AacObject2str(AacObjectType aac_object);
// ts/hls/adts audio header profile to RTMP sequence header object type.
AacObjectType AacTs2rtmp(AacProfile profile);
// RTMP sequence header object type to ts/hls/adts audio header profile.
AacProfile AacRtmp2ts(AacObjectType object_type);

/**
 * the profile for avc/h.264.
 * @see Annex A Profiles and levels, ISO_IEC_14496-10-AVC-2003.pdf, page 205.
 */
enum AvcProfile
{
    AvcProfileReserved = 0,

    // @see ffmpeg, libavcodec/avcodec.h:2713
    AvcProfileBaseline = 66,
    // FF_PROFILE_H264_CONSTRAINED  (1<<9)  // 8+1; constraint_set1_flag
    // FF_PROFILE_H264_CONSTRAINED_BASELINE (66|FF_PROFILE_H264_CONSTRAINED)
    AvcProfileConstrainedBaseline = 578,
    AvcProfileMain = 77,
    AvcProfileExtended = 88,
    AvcProfileHigh = 100,
    AvcProfileHigh10 = 110,
    AvcProfileHigh10Intra = 2158,
    AvcProfileHigh422 = 122,
    AvcProfileHigh422Intra = 2170,
    AvcProfileHigh444 = 144,
    AvcProfileHigh444Predictive = 244,
    AvcProfileHigh444Intra = 2192,
};
std::string AvcProfile2str(AvcProfile profile);

/**
 * the level for avc/h.264.
 * @see Annex A Profiles and levels, ISO_IEC_14496-10-AVC-2003.pdf, page 207.
 */
enum AvcLevel
{
    AvcLevelReserved = 0,

    AvcLevel_1 = 10,
    AvcLevel_11 = 11,
    AvcLevel_12 = 12,
    AvcLevel_13 = 13,
    AvcLevel_2 = 20,
    AvcLevel_21 = 21,
    AvcLevel_22 = 22,
    AvcLevel_3 = 30,
    AvcLevel_31 = 31,
    AvcLevel_32 = 32,
    AvcLevel_4 = 40,
    AvcLevel_41 = 41,
    AvcLevel_5 = 50,
    AvcLevel_51 = 51,
};
std::string AvcLevel2str(AvcLevel level);

/**
 * A sample is the unit of frame.
 * It's a NALU for H.264.
 * It's the whole AAC raw data for AAC.
 * @remark Neither SPS/PPS or ASC is sample unit, it's codec sequence header.
 */
class Sample
{
public:
    // The size of unit.
    int m_size;
    // The ptr of unit, user must free it.
    char* m_bytes;
    // Whether is B frame.
    bool m_bframe;
public:
    Sample();
    Sample(char* b, int s);
    ~Sample();
public:
    // If we need to know whether sample is bframe, we have to parse the NALU payload.
    error ParseBframe();
    // Copy sample, share the bytes pointer.
    Sample* Copy();
};

/**
 * The codec is the information of encoder,
 * corresponding to the sequence header of FLV,
 * parsed to detail info.
 */
class CodecConfig
{
public:
    CodecConfig();
    virtual ~CodecConfig();
};

/**
 * The audio codec info.
 */
class AudioCodecConfig : public CodecConfig
{
    // In FLV specification.
public:
    // The audio codec id; for FLV, it's SoundFormat.
    AudioCodecId m_id;
    // The audio sample rate; for FLV, it's SoundRate.
    AudioSampleRate m_soundRate;
    // The audio sample size, such as 16 bits; for FLV, it's SoundSize.
    AudioSampleBits m_soundSize;
    // The audio number of channels; for FLV, it's SoundType.
    // TODO: FIXME: Rename to sound_channels.
    AudioChannels m_soundType;
    int m_audioDataRate; // in bps
    // In AAC specification.
public:
    /**
     * audio specified
     * audioObjectType, in 1.6.2.1 AudioSpecificConfig, page 33,
     * 1.5.1.1 Audio object type definition, page 23,
     *           in ISO_IEC_14496-3-AAC-2001.pdf.
     */
    AacObjectType m_aacObject;
    /**
     * samplingFrequencyIndex
     */
    uint8_t m_aacSampleRate;
    /**
     * channelConfiguration
     */
    uint8_t m_aacChannels;
    // Sequence header payload.
public:
    /**
     * the aac extra data, the AAC sequence header,
     * without the flv codec header,
     * @see: ffmpeg, AVCodecContext::extradata
     */
    std::vector<char> m_aacExtraData;
public:
    AudioCodecConfig();
    virtual ~AudioCodecConfig();
public:
    virtual bool IsAacCodecOk();
};

/**
 * The video codec info.
 */
class VideoCodecConfig : public CodecConfig
{
public:
    VideoCodecId m_id;
    int m_videoDataRate; // in bps
    double m_frameRate;
    double m_duration;
    int m_width;
    int m_height;
public:
    /**
     * the avc extra data, the AVC sequence header,
     * without the flv codec header,
     * @see: ffmpeg, AVCodecContext::extradata
     */
    std::vector<char> m_avcExtraData;
public:
    /**
     * video specified
     */
    // profile_idc, ISO_IEC_14496-10-AVC-2003.pdf, page 45.
    AvcProfile m_avcProfile;
    // level_idc, ISO_IEC_14496-10-AVC-2003.pdf, page 45.
    AvcLevel m_avcLevel;
    // lengthSizeMinusOne, ISO_IEC_14496-15-AVC-format-2012.pdf, page 16
    int8_t m_NALUnitLength;
    // Note that we may resize the vector, so the under-layer bytes may change.
    std::vector<char> m_sequenceParameterSetNALUnit;
    std::vector<char> m_pictureParameterSetNALUnit;
public:
    // the avc payload format.
    AvcPayloadFormat m_payloadFormat;
public:
    VideoCodecConfig();
    virtual ~VideoCodecConfig();
public:
    virtual bool IsAvcCodecOk();
};

// A frame, consists of a codec and a group of samples.
// TODO: FIXME: Rename to packet to follow names of FFmpeg, which means before decoding or after decoding.
class Frame
{
public:
    // The DTS/PTS in milliseconds, which is TBN=1000.
    int64_t m_dts;
    // PTS = DTS + CTS.
    int32_t m_cts;
public:
    // The codec info of frame.
    CodecConfig* m_codec;
    // The actual parsed number of samples.
    int m_nbSamples;
    // The sampels cache.
    Sample m_samples[MaxNbSamples];
public:
    Frame();
    virtual ~Frame();
public:
    // Initialize the frame, to parse sampels.
    virtual error Initialize(CodecConfig* c);
    // Add a sample to frame.
    virtual error AddSample(char* bytes, int size);
};

// A audio frame, besides a frame, contains the audio frame info, such as frame type.
// TODO: FIXME: Rename to packet to follow names of FFmpeg, which means before decoding or after decoding.
class AudioFrame : public Frame
{
public:
    AudioAacFrameTrait m_aacPacketType;
public:
    AudioFrame();
    virtual ~AudioFrame();
public:
    virtual AudioCodecConfig* Acodec();
};

// A video frame, besides a frame, contains the video frame info, such as frame type.
// TODO: FIXME: Rename to packet to follow names of FFmpeg, which means before decoding or after decoding.
class VideoFrame : public Frame
{
public:
    // video specified
    VideoAvcFrameType m_frameType;
    VideoAvcFrameTrait m_avcPacketType;
    // whether sample_units contains IDR frame.
    bool m_hasIdr;
    // Whether exists AUD NALU.
    bool m_hasAud;
    // Whether exists SPS/PPS NALU.
    bool m_hasSpsPps;
    // The first nalu type.
    AvcNaluType m_firstNaluType;
public:
    VideoFrame();
    virtual ~VideoFrame();
public:
    // Initialize the frame, to parse sampels.
    virtual error Initialize(CodecConfig* c);
    // Add the sample without ANNEXB or IBMF header, or RAW AAC or MP3 data.
    virtual error AddSample(char* bytes, int size);
public:
    virtual VideoCodecConfig* Vcodec();
};

/**
 * A codec format, including one or many stream, each stream identified by a frame.
 * For example, a typical RTMP stream format, consits of a video and audio frame.
 * Maybe some RTMP stream only has a audio stream, for instance, redio application.
 */
class Format
{
public:
    AudioFrame* m_audio;
    AudioCodecConfig* m_acodec;
    VideoFrame* m_video;
    VideoCodecConfig* m_vcodec;
public:
    char* m_raw;
    int m_nbRaw;
public:
    // for sequence header, whether parse the h.264 sps.
    // TODO: FIXME: Refine it.
    bool m_avcParseSps;
    // Whether try to parse in ANNEXB, then by IBMF.
    bool m_tryAnnexbFirst;
public:
    Format();
    virtual ~Format();
public:
    // Initialize the format.
    virtual error Initialize();
    // When got a parsed audio packet.
    // @param data The data in FLV format.
    virtual error OnAudio(int64_t timestamp, char* data, int size);
    // When got a parsed video packet.
    // @param data The data in FLV format.
    virtual error OnVideo(int64_t timestamp, char* data, int size);
    // When got a audio aac sequence header.
    virtual error OnAacSequenceHeader(char* data, int size);
public:
    virtual bool IsAacSequenceHeader();
    virtual bool IsAvcSequenceHeader();
private:
    // Demux the video packet in H.264 codec.
    // The packet is muxed in FLV format, defined in flv specification.
    //          Demux the sps/pps from sequence header.
    //          Demux the samples from NALUs.
    virtual error VideoAvcDemux(Buffer* stream, int64_t timestamp);
private:
    // Parse the H.264 SPS/PPS.
    virtual error AvcDemuxSpsPps(Buffer* stream);
    virtual error AvcDemuxSps();
    virtual error AvcDemuxSpsRbsp(char* rbsp, int nb_rbsp);
private:
    // Parse the H.264 NALUs.
    virtual error VideoNaluDemux(Buffer* stream);
    // Demux the avc NALU in "AnnexB" from ISO_IEC_14496-10-AVC-2003.pdf, page 211.
    virtual error AvcDemuxAnnexbFormat(Buffer* stream);
    virtual error DoAvcDemuxAnnexbFormat(Buffer* stream);
    // Demux the avc NALU in "ISO Base Media File Format" from ISO_IEC_14496-15-AVC-format-2012.pdf, page 20
    virtual error AvcDemuxIbmfFormat(Buffer* stream);
    virtual error DoAvcDemuxIbmfFormat(Buffer* stream);
private:
    // Demux the audio packet in AAC codec.
    //          Demux the asc from sequence header.
    //          Demux the sampels from RAW data.
    virtual error AudioAacDemux(Buffer* stream, int64_t timestamp);
    virtual error AudioMp3Demux(Buffer* stream, int64_t timestamp);
public:
    // Directly demux the sequence header, without RTMP packet header.
    virtual error AudioAacSequenceHeaderDemux(char* data, int size);
};

#endif // CODEC_H
