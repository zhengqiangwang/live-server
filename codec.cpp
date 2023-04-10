#include "codec.h"
#include "buffer.h"
#include "error.h"
#include "utility.h"
#include "core_autofree.h"

const uint8_t kNalTypeMask      = 0x1F;

std::string VideoCodecId2str(VideoCodecId codec)
{
    switch (codec) {
        case VideoCodecIdAVC:
            return "H264";
        case VideoCodecIdOn2VP6:
        case VideoCodecIdOn2VP6WithAlphaChannel:
            return "VP6";
        case VideoCodecIdHEVC:
            return "HEVC";
        case VideoCodecIdAV1:
            return "AV1";
        case VideoCodecIdReserved:
        case VideoCodecIdReserved1:
        case VideoCodecIdReserved2:
        case VideoCodecIdDisabled:
        case VideoCodecIdSorensonH263:
        case VideoCodecIdScreenVideo:
        case VideoCodecIdScreenVideoVersion2:
        default:
            return "Other";
    }
}

std::string AudioCodecId2str(AudioCodecId codec)
{
    switch (codec) {
        case AudioCodecIdAAC:
            return "AAC";
        case AudioCodecIdMP3:
            return "MP3";
        case AudioCodecIdOpus:
            return "Opus";
        case AudioCodecIdReserved1:
        case AudioCodecIdLinearPCMPlatformEndian:
        case AudioCodecIdADPCM:
        case AudioCodecIdLinearPCMLittleEndian:
        case AudioCodecIdNellymoser16kHzMono:
        case AudioCodecIdNellymoser8kHzMono:
        case AudioCodecIdNellymoser:
        case AudioCodecIdReservedG711AlawLogarithmicPCM:
        case AudioCodecIdReservedG711MuLawLogarithmicPCM:
        case AudioCodecIdReserved:
        case AudioCodecIdSpeex:
        case AudioCodecIdReservedMP3_8kHz:
        case AudioCodecIdReservedDeviceSpecificSound:
        default:
            return "Other";
    }
}

std::string AudioSampleRate2str(AudioSampleRate v)
{
    switch (v) {
        case AudioSampleRate5512: return "5512";
        case AudioSampleRate11025: return "11025";
        case AudioSampleRate22050: return "22050";
        case AudioSampleRate44100: return "44100";
        case AudioSampleRateNB8kHz: return "NB8kHz";
        case AudioSampleRateMB12kHz: return "MB12kHz";
        case AudioSampleRateWB16kHz: return "WB16kHz";
        case AudioSampleRateSWB24kHz: return "SWB24kHz";
        case AudioSampleRateFB48kHz: return "FB48kHz";
        default: return "Other";
    }
}

FlvVideo::FlvVideo()
{

}

FlvVideo::~FlvVideo()
{

}

bool FlvVideo::Keyframe(char *data, int size)
{
    // 2bytes required.
    if (size < 1) {
        return false;
    }

    char frame_type = data[0];
    frame_type = (frame_type >> 4) & 0x0F;

    return frame_type == VideoAvcFrameTypeKeyFrame;
}

bool FlvVideo::Sh(char *data, int size)
{
    // sequence header only for h264
    if (!H264(data, size)) {
        return false;
    }

    // 2bytes required.
    if (size < 2) {
        return false;
    }

    char frame_type = data[0];
    frame_type = (frame_type >> 4) & 0x0F;

    char avc_packet_type = data[1];

    return frame_type == VideoAvcFrameTypeKeyFrame
            && avc_packet_type == VideoAvcFrameTraitSequenceHeader;
}

bool FlvVideo::H264(char *data, int size)
{
    // 1bytes required.
    if (size < 1) {
        return false;
    }

    char codec_id = data[0];
    codec_id = codec_id & 0x0F;

    return codec_id == VideoCodecIdAVC;
}

bool FlvVideo::Acceptable(char *data, int size)
{
    // 1bytes required.
    if (size < 1) {
        return false;
    }

    char frame_type = data[0];
    char codec_id = frame_type & 0x0f;
    frame_type = (frame_type >> 4) & 0x0f;

    if (frame_type < 1 || frame_type > 5) {
        return false;
    }

    if (codec_id < 2 || codec_id > 7) {
        return false;
    }

    return true;
}

FlvAudio::FlvAudio()
{

}

FlvAudio::~FlvAudio()
{

}

bool FlvAudio::Sh(char *data, int size)
{
    // sequence header only for aac
    if (!Aac(data, size)) {
        return false;
    }

    // 2bytes required.
    if (size < 2) {
        return false;
    }

    char aac_packet_type = data[1];

    return aac_packet_type == AudioAacFrameTraitSequenceHeader;
}

bool FlvAudio::Aac(char *data, int size)
{
    // 1bytes required.
    if (size < 1) {
        return false;
    }

    char sound_format = data[0];
    sound_format = (sound_format >> 4) & 0x0F;

    return sound_format == AudioCodecIdAAC;
}

/**
 * the public data, event HLS disable, others can use it.
 */
// 0 = 5.5 kHz = 5512 Hz
// 1 = 11 kHz = 11025 Hz
// 2 = 22 kHz = 22050 Hz
// 3 = 44 kHz = 44100 Hz
int flv_srates[] = {5512, 11025, 22050, 44100, 0};

// the sample rates in the codec,
// in the sequence header.
int aac_srates[] =
{
    96000, 88200, 64000, 48000,
    44100, 32000, 24000, 22050,
    16000, 12000, 11025,  8000,
    7350,     0,     0,    0
};

std::string AudioSampleBits2str(AudioSampleBits v)
{
    switch (v) {
        case AudioSampleBits16bit: return "16bits";
        case AudioSampleBits8bit: return "8bits";
        default: return "Other";
    }
}

std::string AudioChannels2str(AudioChannels v)
{
    switch (v) {
        case AudioChannelsStereo: return "Stereo";
        case AudioChannelsMono: return "Mono";
        default: return "Other";
    }
}

std::string AvcNalu2str(AvcNaluType nalu_type)
{
    switch (nalu_type) {
        case AvcNaluTypeNonIDR: return "NonIDR";
        case AvcNaluTypeDataPartitionA: return "DataPartitionA";
        case AvcNaluTypeDataPartitionB: return "DataPartitionB";
        case AvcNaluTypeDataPartitionC: return "DataPartitionC";
        case AvcNaluTypeIDR: return "IDR";
        case AvcNaluTypeSEI: return "SEI";
        case AvcNaluTypeSPS: return "SPS";
        case AvcNaluTypePPS: return "PPS";
        case AvcNaluTypeAccessUnitDelimiter: return "AccessUnitDelimiter";
        case AvcNaluTypeEOSequence: return "EOSequence";
        case AvcNaluTypeEOStream: return "EOStream";
        case AvcNaluTypeFilterData: return "FilterData";
        case AvcNaluTypeSPSExt: return "SPSExt";
        case AvcNaluTypePrefixNALU: return "PrefixNALU";
        case AvcNaluTypeSubsetSPS: return "SubsetSPS";
        case AvcNaluTypeLayerWithoutPartition: return "LayerWithoutPartition";
        case AvcNaluTypeCodedSliceExt: return "CodedSliceExt";
        case AvcNaluTypeReserved: default: return "Other";
    }
}

std::string AacProfile2str(AacProfile aac_profile)
{
    switch (aac_profile) {
        case AacProfileMain: return "Main";
        case AacProfileLC: return "LC";
        case AacProfileSSR: return "SSR";
        default: return "Other";
    }
}

std::string AacObject2str(AacObjectType aac_object)
{
    switch (aac_object) {
        case AacObjectTypeAacMain: return "Main";
        case AacObjectTypeAacHE: return "HE";
        case AacObjectTypeAacHEV2: return "HEv2";
        case AacObjectTypeAacLC: return "LC";
        case AacObjectTypeAacSSR: return "SSR";
        default: return "Other";
    }
}

AacObjectType AacTs2rtmp(AacProfile profile)
{
    switch (profile) {
        case AacProfileMain: return AacObjectTypeAacMain;
        case AacProfileLC: return AacObjectTypeAacLC;
        case AacProfileSSR: return AacObjectTypeAacSSR;
        default: return AacObjectTypeReserved;
    }
}

AacProfile AacRtmp2ts(AacObjectType object_type)
{
    switch (object_type) {
        case AacObjectTypeAacMain: return AacProfileMain;
        case AacObjectTypeAacHE:
        case AacObjectTypeAacHEV2:
        case AacObjectTypeAacLC: return AacProfileLC;
        case AacObjectTypeAacSSR: return AacProfileSSR;
        default: return AacProfileReserved;
    }
}

std::string AvcProfile2str(AvcProfile profile)
{
    switch (profile) {
        case AvcProfileBaseline: return "Baseline";
        case AvcProfileConstrainedBaseline: return "Baseline(Constrained)";
        case AvcProfileMain: return "Main";
        case AvcProfileExtended: return "Extended";
        case AvcProfileHigh: return "High";
        case AvcProfileHigh10: return "High(10)";
        case AvcProfileHigh10Intra: return "High(10+Intra)";
        case AvcProfileHigh422: return "High(422)";
        case AvcProfileHigh422Intra: return "High(422+Intra)";
        case AvcProfileHigh444: return "High(444)";
        case AvcProfileHigh444Predictive: return "High(444+Predictive)";
        case AvcProfileHigh444Intra: return "High(444+Intra)";
        default: return "Other";
    }
}

std::string AvcLevel2str(AvcLevel level)
{
    switch (level) {
        case AvcLevel_1: return "1";
        case AvcLevel_11: return "1.1";
        case AvcLevel_12: return "1.2";
        case AvcLevel_13: return "1.3";
        case AvcLevel_2: return "2";
        case AvcLevel_21: return "2.1";
        case AvcLevel_22: return "2.2";
        case AvcLevel_3: return "3";
        case AvcLevel_31: return "3.1";
        case AvcLevel_32: return "3.2";
        case AvcLevel_4: return "4";
        case AvcLevel_41: return "4.1";
        case AvcLevel_5: return "5";
        case AvcLevel_51: return "5.1";
        default: return "Other";
    }
}

Sample::Sample()
{
    m_size = 0;
    m_bytes = nullptr;
    m_bframe = false;
}

Sample::Sample(char *b, int s)
{
    m_size = s;
    m_bytes = b;
    m_bframe = false;
}

Sample::~Sample()
{

}

error Sample::ParseBframe()
{
    error err = SUCCESS;

    uint8_t header = m_bytes[0];
    AvcNaluType nal_type = (AvcNaluType)(header & kNalTypeMask);

    if (nal_type != AvcNaluTypeNonIDR && nal_type != AvcNaluTypeDataPartitionA && nal_type != AvcNaluTypeIDR) {
        return err;
    }

    Buffer* stream = new Buffer(m_bytes, m_size);
    AutoFree(Buffer, stream);

    // Skip nalu header.
    stream->Skip(1);

    BitBuffer bitstream(stream);
    int32_t first_mb_in_slice = 0;
    if ((err = AvcNaluReadUev(&bitstream, first_mb_in_slice)) != SUCCESS) {
        return ERRORWRAP(err, "nalu read uev");
    }

    int32_t slice_type_v = 0;
    if ((err = AvcNaluReadUev(&bitstream, slice_type_v)) != SUCCESS) {
        return ERRORWRAP(err, "nalu read uev");
    }
    AvcSliceType slice_type = (AvcSliceType)slice_type_v;

    if (slice_type == AvcSliceTypeB || slice_type == AvcSliceTypeB1) {
        m_bframe = true;
        verbose("nal_type=%d, slice type=%d", nal_type, slice_type);
    }

    return err;
}

Sample *Sample::Copy()
{
    Sample* p = new Sample();
    p->m_bytes = m_bytes;
    p->m_size = m_size;
    p->m_bframe = m_bframe;
    return p;
}

CodecConfig::CodecConfig()
{

}

CodecConfig::~CodecConfig()
{

}

AudioCodecConfig::AudioCodecConfig()
{
    m_id = AudioCodecIdForbidden;
    m_soundRate = AudioSampleRateForbidden;
    m_soundSize = AudioSampleBitsForbidden;
    m_soundType = AudioChannelsForbidden;

    m_audioDataRate = 0;

    m_aacObject = AacObjectTypeForbidden;
    m_aacSampleRate = AacSampleRateUnset; // sample rate ignored
    m_aacChannels = 0;
}

AudioCodecConfig::~AudioCodecConfig()
{

}

bool AudioCodecConfig::IsAacCodecOk()
{
    return !m_aacExtraData.empty();
}

VideoCodecConfig::VideoCodecConfig()
{
    m_id = VideoCodecIdForbidden;
    m_videoDataRate = 0;
    m_frameRate = m_duration = 0;

    m_width = 0;
    m_height = 0;

    m_NALUnitLength = 0;
    m_avcProfile = AvcProfileReserved;
    m_avcLevel = AvcLevelReserved;

    m_payloadFormat = AvcPayloadFormatGuess;
}

VideoCodecConfig::~VideoCodecConfig()
{

}

bool VideoCodecConfig::IsAvcCodecOk()
{
    return !m_avcExtraData.empty();
}

Frame::Frame()
{
    m_codec = nullptr;
    m_nbSamples = 0;
    m_dts = 0;
    m_cts = 0;
}

Frame::~Frame()
{

}

error Frame::Initialize(CodecConfig *c)
{
    m_codec = c;
    m_nbSamples = 0;
    m_dts = 0;
    m_cts = 0;
    return SUCCESS;
}

error Frame::AddSample(char *bytes, int size)
{
    error err = SUCCESS;

    if (m_nbSamples >= MaxNbSamples) {
        return ERRORNEW(ERROR_HLS_DECODE_ERROR, "Frame samples overflow");
    }

    Sample* sample = &m_samples[m_nbSamples++];
    sample->m_bytes = bytes;
    sample->m_size = size;
    sample->m_bframe = false;

    return err;
}

AudioFrame::AudioFrame()
{
    m_aacPacketType = AudioAacFrameTraitForbidden;
}

AudioFrame::~AudioFrame()
{

}

AudioCodecConfig *AudioFrame::Acodec()
{
    return (AudioCodecConfig*)m_codec;
}

VideoFrame::VideoFrame()
{
    m_frameType = VideoAvcFrameTypeForbidden;
    m_avcPacketType = VideoAvcFrameTraitForbidden;
    m_hasIdr = m_hasAud = m_hasSpsPps = false;
    m_firstNaluType = AvcNaluTypeForbidden;
}

VideoFrame::~VideoFrame()
{

}

error VideoFrame::Initialize(CodecConfig *c)
{
    m_firstNaluType = AvcNaluTypeForbidden;
    m_hasIdr = m_hasSpsPps = m_hasAud = false;
    return Frame::Initialize(c);
}

error VideoFrame::AddSample(char *bytes, int size)
{
    error err = SUCCESS;

    if ((err = Frame::AddSample(bytes, size)) != SUCCESS) {
        return ERRORWRAP(err, "add frame");
    }

    // for video, parse the nalu type, set the IDR flag.
    AvcNaluType nal_unit_type = (AvcNaluType)(bytes[0] & 0x1f);

    if (nal_unit_type == AvcNaluTypeIDR) {
        m_hasIdr = true;
    } else if (nal_unit_type == AvcNaluTypeSPS || nal_unit_type == AvcNaluTypePPS) {
        m_hasSpsPps = true;
    } else if (nal_unit_type == AvcNaluTypeAccessUnitDelimiter) {
        m_hasAud = true;
    }

    if (m_firstNaluType == AvcNaluTypeReserved) {
        m_firstNaluType = nal_unit_type;
    }

    return err;
}

VideoCodecConfig *VideoFrame::Vcodec()
{
    return (VideoCodecConfig*)m_codec;
}

Format::Format()
{
    m_acodec = NULL;
    m_vcodec = NULL;
    m_audio = NULL;
    m_video = NULL;
    m_avcParseSps = true;
    m_tryAnnexbFirst = true;
    m_raw = NULL;
    m_nbRaw = 0;
}

Format::~Format()
{
    Freep(m_audio);
    Freep(m_video);
    Freep(m_acodec);
    Freep(m_vcodec);
}

error Format::Initialize()
{
    return SUCCESS;
}

error Format::OnAudio(int64_t timestamp, char *data, int size)
{
    error err = SUCCESS;

    if (!data || size <= 0) {
        info("no audio present, ignore it.");
        return err;
    }

    Buffer* buffer = new Buffer(data, size);
    AutoFree(Buffer, buffer);

    // We already checked the size is positive and data is not NULL.
    Assert(buffer->Require(1));

    // @see: E.4.2 Audio Tags, video_file_format_spec_v10_1.pdf, page 76
    uint8_t v = buffer->Read1Bytes();
    AudioCodecId codec = (AudioCodecId)((v >> 4) & 0x0f);

    if (codec != AudioCodecIdMP3 && codec != AudioCodecIdAAC) {
        return err;
    }

    if (!m_acodec) {
        m_acodec = new AudioCodecConfig();
    }
    if (!m_audio) {
        m_audio = new AudioFrame();
    }

    if ((err = m_audio->Initialize(m_acodec)) != SUCCESS) {
        return ERRORWRAP(err, "init audio");
    }

    // Parse by specified codec.
    buffer->Skip(-1 * buffer->Pos());

    if (codec == AudioCodecIdMP3) {
        return AudioMp3Demux(buffer, timestamp);
    }

    return AudioAacDemux(buffer, timestamp);
}

error Format::OnVideo(int64_t timestamp, char *data, int size)
{
    error err = SUCCESS;

    if (!data || size <= 0) {
        trace("no video present, ignore it.");
        return err;
    }

    Buffer* buffer = new Buffer(data, size);
    AutoFree(Buffer, buffer);

    // We already checked the size is positive and data is not NULL.
    Assert(buffer->Require(1));

    // @see: E.4.3 Video Tags, video_file_format_spec_v10_1.pdf, page 78
    int8_t frame_type = buffer->Read1Bytes();
    VideoCodecId codec_id = (VideoCodecId)(frame_type & 0x0f);

    // TODO: Support other codecs.
    if (codec_id != VideoCodecIdAVC) {
        return err;
    }

    if (!m_vcodec) {
        m_vcodec = new VideoCodecConfig();
    }
    if (!m_video) {
        m_video = new VideoFrame();
    }

    if ((err = m_video->Initialize(m_vcodec)) != SUCCESS) {
        return ERRORWRAP(err, "init video");
    }

    buffer->Skip(-1 * buffer->Pos());
    return VideoAvcDemux(buffer, timestamp);
}

error Format::OnAacSequenceHeader(char *data, int size)
{
    error err = SUCCESS;

    if (!m_acodec) {
        m_acodec = new AudioCodecConfig();
    }
    if (!m_audio) {
        m_audio = new AudioFrame();
    }

    if ((err = m_audio->Initialize(m_acodec)) != SUCCESS) {
        return ERRORWRAP(err, "init audio");
    }

    return AudioAacSequenceHeaderDemux(data, size);
}

bool Format::IsAacSequenceHeader()
{
    return m_acodec && m_acodec->m_id == AudioCodecIdAAC
            && m_audio && m_audio->m_aacPacketType == AudioAacFrameTraitSequenceHeader;
}

bool Format::IsAvcSequenceHeader()
{
    bool h264 = (m_vcodec && m_vcodec->m_id == VideoCodecIdAVC);
    bool h265 = (m_vcodec && m_vcodec->m_id == VideoCodecIdHEVC);
    bool av1 = (m_vcodec && m_vcodec->m_id == VideoCodecIdAV1);
    return m_vcodec && (h264 || h265 || av1)
            && m_video && m_video->m_avcPacketType == VideoAvcFrameTraitSequenceHeader;
}

error Format::VideoAvcDemux(Buffer *stream, int64_t timestamp)
{
    error err = SUCCESS;

    // @see: E.4.3 Video Tags, video_file_format_spec_v10_1.pdf, page 78
    int8_t frame_type = stream->Read1Bytes();
    VideoCodecId codec_id = (VideoCodecId)(frame_type & 0x0f);
    frame_type = (frame_type >> 4) & 0x0f;

    m_video->m_frameType = (VideoAvcFrameType)frame_type;

    // ignore info frame without error,
    // @see https://github.com/ossrs/srs/issues/288#issuecomment-69863909
    if (m_video->m_frameType == VideoAvcFrameTypeVideoInfoFrame) {
        warn("avc igone the info frame");
        return err;
    }

    // only support h.264/avc
    if (codec_id != VideoCodecIdAVC) {
        return ERRORNEW(ERROR_HLS_DECODE_ERROR, "avc only support video h.264/avc, actual=%d", codec_id);
    }
    m_vcodec->m_id = codec_id;

    if (!stream->Require(4)) {
        return ERRORNEW(ERROR_HLS_DECODE_ERROR, "avc decode avc_packet_type");
    }
    int8_t avc_packet_type = stream->Read1Bytes();
    int32_t composition_time = stream->Read3Bytes();

    // pts = dts + cts.
    m_video->m_dts = timestamp;
    m_video->m_cts = composition_time;
    m_video->m_avcPacketType = (VideoAvcFrameTrait)avc_packet_type;

    // Update the RAW AVC data.
    m_raw = stream->Data() + stream->Pos();
    m_nbRaw = stream->Size() - stream->Pos();

    if (avc_packet_type == VideoAvcFrameTraitSequenceHeader) {
        // TODO: FIXME: Maybe we should ignore any error for parsing sps/pps.
        if ((err = AvcDemuxSpsPps(stream)) != SUCCESS) {
            return ERRORWRAP(err, "demux SPS/PPS");
        }
    } else if (avc_packet_type == VideoAvcFrameTraitNALU){
        if ((err = VideoNaluDemux(stream)) != SUCCESS) {
            return ERRORWRAP(err, "demux NALU");
        }
    } else {
        // ignored.
    }

    return err;
}

// For media server, we don't care the codec, so we just try to parse sps-pps, and we could ignore any error if fail.
// LCOV_EXCL_START

error Format::AvcDemuxSpsPps(Buffer *stream)
{
    // AVCDecoderConfigurationRecord
    // 5.2.4.1.1 Syntax, ISO_IEC_14496-15-AVC-format-2012.pdf, page 16
    int avc_extra_size = stream->Size() - stream->Pos();
    if (avc_extra_size > 0) {
        char *copy_stream_from = stream->Data() + stream->Pos();
        m_vcodec->m_avcExtraData = std::vector<char>(copy_stream_from, copy_stream_from + avc_extra_size);
    }

    if (!stream->Require(6)) {
        return ERRORNEW(ERROR_HLS_DECODE_ERROR, "avc decode sequence header");
    }
    //int8_t configurationVersion = stream->read_1bytes();
    stream->Read1Bytes();
    //int8_t AVCProfileIndication = stream->read_1bytes();
    m_vcodec->m_avcProfile = (AvcProfile)stream->Read1Bytes();
    //int8_t profile_compatibility = stream->read_1bytes();
    stream->Read1Bytes();
    //int8_t AVCLevelIndication = stream->read_1bytes();
    m_vcodec->m_avcLevel = (AvcLevel)stream->Read1Bytes();

    // parse the NALU size.
    int8_t lengthSizeMinusOne = stream->Read1Bytes();
    lengthSizeMinusOne &= 0x03;
    m_vcodec->m_NALUnitLength = lengthSizeMinusOne;

    // 5.3.4.2.1 Syntax, ISO_IEC_14496-15-AVC-format-2012.pdf, page 16
    // 5.2.4.1 AVC decoder configuration record
    // 5.2.4.1.2 Semantics
    // The value of this field shall be one of 0, 1, or 3 corresponding to a
    // length encoded with 1, 2, or 4 bytes, respectively.
    if (m_vcodec->m_NALUnitLength == 2) {
        return ERRORNEW(ERROR_HLS_DECODE_ERROR, "sps lengthSizeMinusOne should never be 2");
    }

    // 1 sps, 7.3.2.1 Sequence parameter set RBSP syntax
    // ISO_IEC_14496-10-AVC-2003.pdf, page 45.
    if (!stream->Require(1)) {
        return ERRORNEW(ERROR_HLS_DECODE_ERROR, "decode SPS");
    }
    int8_t numOfSequenceParameterSets = stream->Read1Bytes();
    numOfSequenceParameterSets &= 0x1f;
    if (numOfSequenceParameterSets < 1) {
        return ERRORNEW(ERROR_HLS_DECODE_ERROR, "decode SPS");
    }
    // Support for multiple SPS, then pick the first non-empty one.
    for (int i = 0; i < numOfSequenceParameterSets; ++i) {
        if (!stream->Require(2)) {
            return ERRORNEW(ERROR_HLS_DECODE_ERROR, "decode SPS size");
        }
        uint16_t sequenceParameterSetLength = stream->Read2Bytes();
        if (!stream->Require(sequenceParameterSetLength)) {
            return ERRORNEW(ERROR_HLS_DECODE_ERROR, "decode SPS data");
        }
        if (sequenceParameterSetLength > 0) {
            m_vcodec->m_sequenceParameterSetNALUnit.resize(sequenceParameterSetLength);
            stream->ReadBytes(&m_vcodec->m_sequenceParameterSetNALUnit[0], sequenceParameterSetLength);
        }
    }

    // 1 pps
    if (!stream->Require(1)) {
        return ERRORNEW(ERROR_HLS_DECODE_ERROR, "decode PPS");
    }
    int8_t numOfPictureParameterSets = stream->Read1Bytes();
    numOfPictureParameterSets &= 0x1f;
    if (numOfPictureParameterSets < 1) {
        return ERRORNEW(ERROR_HLS_DECODE_ERROR, "decode SPS");
    }
    // Support for multiple PPS, then pick the first non-empty one.
    for (int i = 0; i < numOfPictureParameterSets; ++i) {
        if (!stream->Require(2)) {
            return ERRORNEW(ERROR_HLS_DECODE_ERROR, "decode PPS size");
        }
        uint16_t pictureParameterSetLength = stream->Read2Bytes();
        if (!stream->Require(pictureParameterSetLength)) {
            return ERRORNEW(ERROR_HLS_DECODE_ERROR, "decode PPS data");
        }
        if (pictureParameterSetLength > 0) {
            m_vcodec->m_pictureParameterSetNALUnit.resize(pictureParameterSetLength);
            stream->ReadBytes(&m_vcodec->m_pictureParameterSetNALUnit[0], pictureParameterSetLength);
        }
    }
    return AvcDemuxSps();
}

error Format::AvcDemuxSps()
{
    error err = SUCCESS;

    if (m_vcodec->m_sequenceParameterSetNALUnit.empty()) {
        return err;
    }

    char* sps = &m_vcodec->m_sequenceParameterSetNALUnit[0];
    int nbsps = (int)m_vcodec->m_sequenceParameterSetNALUnit.size();

    Buffer stream(sps, nbsps);

    // for NALU, 7.3.1 NAL unit syntax
    // ISO_IEC_14496-10-AVC-2012.pdf, page 61.
    if (!stream.Require(1)) {
        return ERRORNEW(ERROR_HLS_DECODE_ERROR, "decode SPS");
    }
    int8_t nutv = stream.Read1Bytes();

    // forbidden_zero_bit shall be equal to 0.
    int8_t forbidden_zero_bit = (nutv >> 7) & 0x01;
    if (forbidden_zero_bit) {
        return ERRORNEW(ERROR_HLS_DECODE_ERROR, "forbidden_zero_bit shall be equal to 0");
    }

    // nal_ref_idc not equal to 0 specifies that the content of the NAL unit contains a sequence parameter set or a picture
    // parameter set or a slice of a reference picture or a slice data partition of a reference picture.
    int8_t nal_ref_idc = (nutv >> 5) & 0x03;
    if (!nal_ref_idc) {
        return ERRORNEW(ERROR_HLS_DECODE_ERROR, "for sps, nal_ref_idc shall be not be equal to 0");
    }

    // 7.4.1 NAL unit semantics
    // ISO_IEC_14496-10-AVC-2012.pdf, page 61.
    // nal_unit_type specifies the type of RBSP data structure contained in the NAL unit as specified in Table 7-1.
    AvcNaluType nal_unit_type = (AvcNaluType)(nutv & 0x1f);
    if (nal_unit_type != 7) {
        return ERRORNEW(ERROR_HLS_DECODE_ERROR, "for sps, nal_unit_type shall be equal to 7");
    }

    // decode the rbsp from sps.
    // rbsp[ i ] a raw byte sequence payload is specified as an ordered sequence of bytes.
    std::vector<int8_t> rbsp(m_vcodec->m_sequenceParameterSetNALUnit.size());

    int nb_rbsp = 0;
    while (!stream.Empty()) {
        rbsp[nb_rbsp] = stream.Read1Bytes();

        // XX 00 00 03 XX, the 03 byte should be drop.
        if (nb_rbsp > 2 && rbsp[nb_rbsp - 2] == 0 && rbsp[nb_rbsp - 1] == 0 && rbsp[nb_rbsp] == 3) {
            // read 1byte more.
            if (stream.Empty()) {
                break;
            }
            rbsp[nb_rbsp] = stream.Read1Bytes();
            nb_rbsp++;

            continue;
        }

        nb_rbsp++;
    }

    return AvcDemuxSpsRbsp((char*)&rbsp[0], nb_rbsp);
}

error Format::AvcDemuxSpsRbsp(char *rbsp, int nb_rbsp)
{
    error err = SUCCESS;

    // we donot parse the detail of sps.
    // @see https://github.com/ossrs/srs/issues/474
    if (!m_avcParseSps) {
        return err;
    }

    // reparse the rbsp.
    Buffer stream(rbsp, nb_rbsp);

    // for SPS, 7.3.2.1.1 Sequence parameter set data syntax
    // ISO_IEC_14496-10-AVC-2012.pdf, page 62.
    if (!stream.Require(3)) {
        return ERRORNEW(ERROR_HLS_DECODE_ERROR, "sps shall atleast 3bytes");
    }
    uint8_t profile_idc = stream.Read1Bytes();
    if (!profile_idc) {
        return ERRORNEW(ERROR_HLS_DECODE_ERROR, "sps the profile_idc invalid");
    }

    int8_t flags = stream.Read1Bytes();
    if (flags & 0x03) {
        return ERRORNEW(ERROR_HLS_DECODE_ERROR, "sps the flags invalid");
    }

    uint8_t level_idc = stream.Read1Bytes();
    if (!level_idc) {
        return ERRORNEW(ERROR_HLS_DECODE_ERROR, "sps the level_idc invalid");
    }

    BitBuffer bs(&stream);

    int32_t seq_parameter_set_id = -1;
    if ((err = AvcNaluReadUev(&bs, seq_parameter_set_id)) != SUCCESS) {
        return ERRORWRAP(err, "read seq_parameter_set_id");
    }
    if (seq_parameter_set_id < 0) {
        return ERRORNEW(ERROR_HLS_DECODE_ERROR, "sps the seq_parameter_set_id invalid");
    }

    int32_t chroma_format_idc = -1;
    if (profile_idc == 100 || profile_idc == 110 || profile_idc == 122 || profile_idc == 244
        || profile_idc == 44 || profile_idc == 83 || profile_idc == 86 || profile_idc == 118
        || profile_idc == 128) {
        if ((err = AvcNaluReadUev(&bs, chroma_format_idc)) != SUCCESS) {
            return ERRORWRAP(err, "read chroma_format_idc");
        }
        if (chroma_format_idc == 3) {
            int8_t separate_colour_plane_flag = -1;
            if ((err = AvcNaluReadBit(&bs, separate_colour_plane_flag)) != SUCCESS) {
                return ERRORWRAP(err, "read separate_colour_plane_flag");
            }
        }

        int32_t bit_depth_luma_minus8 = -1;
        if ((err = AvcNaluReadUev(&bs, bit_depth_luma_minus8)) != SUCCESS) {
            return ERRORWRAP(err, "read bit_depth_luma_minus8");;
        }

        int32_t bit_depth_chroma_minus8 = -1;
        if ((err = AvcNaluReadUev(&bs, bit_depth_chroma_minus8)) != SUCCESS) {
            return ERRORWRAP(err, "read bit_depth_chroma_minus8");;
        }

        int8_t qpprime_y_zero_transform_bypass_flag = -1;
        if ((err = AvcNaluReadBit(&bs, qpprime_y_zero_transform_bypass_flag)) != SUCCESS) {
            return ERRORWRAP(err, "read qpprime_y_zero_transform_bypass_flag");;
        }

        int8_t seq_scaling_matrix_present_flag = -1;
        if ((err = AvcNaluReadBit(&bs, seq_scaling_matrix_present_flag)) != SUCCESS) {
            return ERRORWRAP(err, "read seq_scaling_matrix_present_flag");;
        }
        if (seq_scaling_matrix_present_flag) {
            int nb_scmpfs = ((chroma_format_idc != 3)? 8:12);
            for (int i = 0; i < nb_scmpfs; i++) {
                int8_t seq_scaling_matrix_present_flag_i = -1;
                if ((err = AvcNaluReadBit(&bs, seq_scaling_matrix_present_flag_i)) != SUCCESS) {
                    return ERRORWRAP(err, "read seq_scaling_matrix_present_flag_i");;
                }
            }
        }
    }

    int32_t log2_max_frame_num_minus4 = -1;
    if ((err = AvcNaluReadUev(&bs, log2_max_frame_num_minus4)) != SUCCESS) {
        return ERRORWRAP(err, "read log2_max_frame_num_minus4");;
    }

    int32_t pic_order_cnt_type = -1;
    if ((err = AvcNaluReadUev(&bs, pic_order_cnt_type)) != SUCCESS) {
        return ERRORWRAP(err, "read pic_order_cnt_type");;
    }

    if (pic_order_cnt_type == 0) {
        int32_t log2_max_pic_order_cnt_lsb_minus4 = -1;
        if ((err = AvcNaluReadUev(&bs, log2_max_pic_order_cnt_lsb_minus4)) != SUCCESS) {
            return ERRORWRAP(err, "read log2_max_pic_order_cnt_lsb_minus4");;
        }
    } else if (pic_order_cnt_type == 1) {
        int8_t delta_pic_order_always_zero_flag = -1;
        if ((err = AvcNaluReadBit(&bs, delta_pic_order_always_zero_flag)) != SUCCESS) {
            return ERRORWRAP(err, "read delta_pic_order_always_zero_flag");;
        }

        int32_t offset_for_non_ref_pic = -1;
        if ((err = AvcNaluReadUev(&bs, offset_for_non_ref_pic)) != SUCCESS) {
            return ERRORWRAP(err, "read offset_for_non_ref_pic");;
        }

        int32_t offset_for_top_to_bottom_field = -1;
        if ((err = AvcNaluReadUev(&bs, offset_for_top_to_bottom_field)) != SUCCESS) {
            return ERRORWRAP(err, "read offset_for_top_to_bottom_field");;
        }

        int32_t num_ref_frames_in_pic_order_cnt_cycle = -1;
        if ((err = AvcNaluReadUev(&bs, num_ref_frames_in_pic_order_cnt_cycle)) != SUCCESS) {
            return ERRORWRAP(err, "read num_ref_frames_in_pic_order_cnt_cycle");;
        }
        if (num_ref_frames_in_pic_order_cnt_cycle < 0) {
            return ERRORNEW(ERROR_HLS_DECODE_ERROR, "sps the num_ref_frames_in_pic_order_cnt_cycle");
        }
        for (int i = 0; i < num_ref_frames_in_pic_order_cnt_cycle; i++) {
            int32_t offset_for_ref_frame_i = -1;
            if ((err = AvcNaluReadUev(&bs, offset_for_ref_frame_i)) != SUCCESS) {
                return ERRORWRAP(err, "read offset_for_ref_frame_i");;
            }
        }
    }

    int32_t max_num_ref_frames = -1;
    if ((err = AvcNaluReadUev(&bs, max_num_ref_frames)) != SUCCESS) {
        return ERRORWRAP(err, "read max_num_ref_frames");;
    }

    int8_t gaps_in_frame_num_value_allowed_flag = -1;
    if ((err = AvcNaluReadBit(&bs, gaps_in_frame_num_value_allowed_flag)) != SUCCESS) {
        return ERRORWRAP(err, "read gaps_in_frame_num_value_allowed_flag");;
    }

    int32_t pic_width_in_mbs_minus1 = -1;
    if ((err = AvcNaluReadUev(&bs, pic_width_in_mbs_minus1)) != SUCCESS) {
        return ERRORWRAP(err, "read pic_width_in_mbs_minus1");;
    }

    int32_t pic_height_in_map_units_minus1 = -1;
    if ((err = AvcNaluReadUev(&bs, pic_height_in_map_units_minus1)) != SUCCESS) {
        return ERRORWRAP(err, "read pic_height_in_map_units_minus1");;
    }

    int8_t frame_mbs_only_flag = -1;
    if ((err = AvcNaluReadBit(&bs, frame_mbs_only_flag)) != SUCCESS) {
        return ERRORWRAP(err, "read frame_mbs_only_flag");;
    }
    if(!frame_mbs_only_flag) {
        /* Skip mb_adaptive_frame_field_flag */
        int8_t mb_adaptive_frame_field_flag = -1;
        if ((err = AvcNaluReadBit(&bs, mb_adaptive_frame_field_flag)) != SUCCESS) {
            return ERRORWRAP(err, "read mb_adaptive_frame_field_flag");;
        }
    }

    /* Skip direct_8x8_inference_flag */
    int8_t direct_8x8_inference_flag = -1;
    if ((err = AvcNaluReadBit(&bs, direct_8x8_inference_flag)) != SUCCESS) {
        return ERRORWRAP(err, "read direct_8x8_inference_flag");;
    }

    /* We need the following value to evaluate offsets, if any */
    int8_t frame_cropping_flag = -1;
    if ((err = AvcNaluReadBit(&bs, frame_cropping_flag)) != SUCCESS) {
        return ERRORWRAP(err, "read frame_cropping_flag");;
    }
    int32_t frame_crop_left_offset = 0, frame_crop_right_offset = 0,
            frame_crop_top_offset = 0, frame_crop_bottom_offset = 0;
    if(frame_cropping_flag) {
        if ((err = AvcNaluReadUev(&bs, frame_crop_left_offset)) != SUCCESS) {
            return ERRORWRAP(err, "read frame_crop_left_offset");;
        }
        if ((err = AvcNaluReadUev(&bs, frame_crop_right_offset)) != SUCCESS) {
            return ERRORWRAP(err, "read frame_crop_right_offset");;
        }
        if ((err = AvcNaluReadUev(&bs, frame_crop_top_offset)) != SUCCESS) {
            return ERRORWRAP(err, "read frame_crop_top_offset");;
        }
        if ((err = AvcNaluReadUev(&bs, frame_crop_bottom_offset)) != SUCCESS) {
            return ERRORWRAP(err, "read frame_crop_bottom_offset");;
        }
    }

    /* Skip vui_parameters_present_flag */
    int8_t vui_parameters_present_flag = -1;
    if ((err = AvcNaluReadBit(&bs, vui_parameters_present_flag)) != SUCCESS) {
        return ERRORWRAP(err, "read vui_parameters_present_flag");;
    }

    m_vcodec->m_width = ((pic_width_in_mbs_minus1 + 1) * 16) - frame_crop_left_offset * 2 - frame_crop_right_offset * 2;
    m_vcodec->m_height = ((2 - frame_mbs_only_flag) * (pic_height_in_map_units_minus1 + 1) * 16) \
                    - (frame_crop_top_offset * 2) - (frame_crop_bottom_offset * 2);

    return err;
}

// LCOV_EXCL_STOP

error Format::VideoNaluDemux(Buffer *stream)
{
    error err = SUCCESS;

    // ensure the sequence header demuxed
    if (!m_vcodec->IsAvcCodecOk()) {
        warn("avc ignore type=%d for no sequence header", VideoAvcFrameTraitNALU);
        return err;
    }

    // Parse the SPS/PPS in ANNEXB or IBMF format.
    if (m_vcodec->m_payloadFormat == AvcPayloadFormatIbmf) {
        if ((err = AvcDemuxIbmfFormat(stream)) != SUCCESS) {
            return ERRORWRAP(err, "avc demux ibmf");
        }
    } else if (m_vcodec->m_payloadFormat == AvcPayloadFormatAnnexb) {
        if ((err = AvcDemuxAnnexbFormat(stream)) != SUCCESS) {
            return ERRORWRAP(err, "avc demux annexb");
        }
    } else {
        if ((err = m_tryAnnexbFirst ? AvcDemuxAnnexbFormat(stream) : AvcDemuxIbmfFormat(stream)) == SUCCESS) {
            m_vcodec->m_payloadFormat = m_tryAnnexbFirst ? AvcPayloadFormatAnnexb : AvcPayloadFormatIbmf;
        } else {
            Freep(err);
            if ((err = m_tryAnnexbFirst ? AvcDemuxIbmfFormat(stream) : AvcDemuxAnnexbFormat(stream)) == SUCCESS) {
                m_vcodec->m_payloadFormat = m_tryAnnexbFirst ? AvcPayloadFormatIbmf : AvcPayloadFormatAnnexb;
            } else {
                return ERRORWRAP(err, "avc demux try_annexb_first=%d", m_tryAnnexbFirst);
            }
        }
    }

    return err;
}

error Format::AvcDemuxAnnexbFormat(Buffer *stream)
{
    error err = SUCCESS;

    int pos = stream->Pos();
    err = DoAvcDemuxAnnexbFormat(stream);

    // Restore the stream if error.
    if (err != SUCCESS) {
        stream->Skip(pos - stream->Pos());
    }

    return err;
}

error Format::DoAvcDemuxAnnexbFormat(Buffer *stream)
{
    error err = SUCCESS;

    // not annexb, try others
    if (!AvcStartswithAnnexb(stream, NULL)) {
        return ERRORNEW(ERROR_HLS_DECODE_ERROR, "not annexb");
    }

    // AnnexB
    // B.1.1 Byte stream NAL unit syntax,
    // ISO_IEC_14496-10-AVC-2003.pdf, page 211.
    while (!stream->Empty()) {
        // find start code
        int nb_start_code = 0;
        if (!AvcStartswithAnnexb(stream, &nb_start_code)) {
            return err;
        }

        // skip the start code.
        if (nb_start_code > 0) {
            stream->Skip(nb_start_code);
        }

        // the NALU start bytes.
        char* p = stream->Data() + stream->Pos();

        // get the last matched NALU
        while (!stream->Empty()) {
            if (AvcStartswithAnnexb(stream, NULL)) {
                break;
            }

            stream->Skip(1);
        }

        char* pp = stream->Data() + stream->Pos();

        // skip the empty.
        if (pp - p <= 0) {
            continue;
        }

        // got the NALU.
        if ((err = m_video->AddSample(p, (int)(pp - p))) != SUCCESS) {
            return ERRORWRAP(err, "add video frame");
        }
    }

    return err;
}

error Format::AvcDemuxIbmfFormat(Buffer *stream)
{
    error err = SUCCESS;

    int pos = stream->Pos();
    err = DoAvcDemuxIbmfFormat(stream);

    // Restore the stream if error.
    if (err != SUCCESS) {
        stream->Skip(pos - stream->Pos());
    }

    return err;
}

error Format::DoAvcDemuxIbmfFormat(Buffer *stream)
{
    error err = SUCCESS;

    int PictureLength = stream->Size() - stream->Pos();

    // 5.3.4.2.1 Syntax, ISO_IEC_14496-15-AVC-format-2012.pdf, page 16
    // 5.2.4.1 AVC decoder configuration record
    // 5.2.4.1.2 Semantics
    // The value of this field shall be one of 0, 1, or 3 corresponding to a
    // length encoded with 1, 2, or 4 bytes, respectively.
    Assert(m_vcodec->m_NALUnitLength != 2);

    // 5.3.4.2.1 Syntax, ISO_IEC_14496-15-AVC-format-2012.pdf, page 20
    for (int i = 0; i < PictureLength;) {
        // unsigned int((NAL_unit_length+1)*8) NALUnitLength;
        if (!stream->Require(m_vcodec->m_NALUnitLength + 1)) {
            return ERRORNEW(ERROR_HLS_DECODE_ERROR, "avc decode NALU size");
        }
        int32_t NALUnitLength = 0;
        if (m_vcodec->m_NALUnitLength == 3) {
            NALUnitLength = stream->Read4Bytes();
        } else if (m_vcodec->m_NALUnitLength == 1) {
            NALUnitLength = stream->Read2Bytes();
        } else {
            NALUnitLength = stream->Read1Bytes();
        }

        // maybe stream is invalid format.
        // see: https://github.com/ossrs/srs/issues/183
        if (NALUnitLength < 0) {
            return ERRORNEW(ERROR_HLS_DECODE_ERROR, "maybe stream is AnnexB format");
        }

        // NALUnit
        if (!stream->Require(NALUnitLength)) {
            return ERRORNEW(ERROR_HLS_DECODE_ERROR, "avc decode NALU data");
        }
        // 7.3.1 NAL unit syntax, ISO_IEC_14496-10-AVC-2003.pdf, page 44.
        if ((err = m_video->AddSample(stream->Data() + stream->Pos(), NALUnitLength)) != SUCCESS) {
            return ERRORWRAP(err, "avc add video frame");
        }
        stream->Skip(NALUnitLength);

        i += m_vcodec->m_NALUnitLength + 1 + NALUnitLength;
    }

    return err;
}

error Format::AudioAacDemux(Buffer *stream, int64_t timestamp)
{
    error err = SUCCESS;

    m_audio->m_cts = 0;
    m_audio->m_dts = timestamp;

    // @see: E.4.2 Audio Tags, video_file_format_spec_v10_1.pdf, page 76
    int8_t sound_format = stream->Read1Bytes();

    int8_t sound_type = sound_format & 0x01;
    int8_t sound_size = (sound_format >> 1) & 0x01;
    int8_t sound_rate = (sound_format >> 2) & 0x03;
    sound_format = (sound_format >> 4) & 0x0f;

    AudioCodecId codec_id = (AudioCodecId)sound_format;
    m_acodec->m_id = codec_id;

    m_acodec->m_soundType = (AudioChannels)sound_type;
    m_acodec->m_soundRate = (AudioSampleRate)sound_rate;
    m_acodec->m_soundSize = (AudioSampleBits)sound_size;

    // we support h.264+mp3 for hls.
    if (codec_id == AudioCodecIdMP3) {
        return ERRORNEW(ERROR_HLS_TRY_MP3, "try mp3");
    }

    // only support aac
    if (codec_id != AudioCodecIdAAC) {
        return ERRORNEW(ERROR_HLS_DECODE_ERROR, "not supported codec %d", codec_id);
    }

    if (!stream->Require(1)) {
        return ERRORNEW(ERROR_HLS_DECODE_ERROR, "aac decode aac_packet_type");
    }

    AudioAacFrameTrait aac_packet_type = (AudioAacFrameTrait)stream->Read1Bytes();
    m_audio->m_aacPacketType = (AudioAacFrameTrait)aac_packet_type;

    // Update the RAW AAC data.
    m_raw = stream->Data() + stream->Pos();
    m_nbRaw = stream->Size() - stream->Pos();

    if (aac_packet_type == AudioAacFrameTraitSequenceHeader) {
        // AudioSpecificConfig
        // 1.6.2.1 AudioSpecificConfig, in ISO_IEC_14496-3-AAC-2001.pdf, page 33.
        int aac_extra_size = stream->Size() - stream->Pos();
        if (aac_extra_size > 0) {
            char *copy_stream_from = stream->Data() + stream->Pos();
            m_acodec->m_aacExtraData = std::vector<char>(copy_stream_from, copy_stream_from + aac_extra_size);

            if ((err = AudioAacSequenceHeaderDemux(&m_acodec->m_aacExtraData[0], aac_extra_size)) != SUCCESS) {
                return ERRORWRAP(err, "demux aac sh");
            }
        }
    } else if (aac_packet_type == AudioAacFrameTraitRawData) {
        // ensure the sequence header demuxed
        if (!m_acodec->IsAacCodecOk()) {
            warn("aac ignore type=%d for no sequence header", aac_packet_type);
            return err;
        }

        // Raw AAC frame data in UI8 []
        // 6.3 Raw Data, ISO_IEC_13818-7-AAC-2004.pdf, page 28
        if ((err = m_audio->AddSample(stream->Data() + stream->Pos(), stream->Size() - stream->Pos())) != SUCCESS) {
            return ERRORWRAP(err, "add audio frame");
        }
    } else {
        // ignored.
    }

    // reset the sample rate by sequence header
    if (m_acodec->m_aacSampleRate != AacSampleRateUnset) {
        static int aac_srates[] = {
            96000, 88200, 64000, 48000,
            44100, 32000, 24000, 22050,
            16000, 12000, 11025,  8000,
            7350,     0,     0,    0
        };
        switch (aac_srates[m_acodec->m_aacSampleRate]) {
            case 11025:
                m_acodec->m_soundRate = AudioSampleRate11025;
                break;
            case 22050:
                m_acodec->m_soundRate = AudioSampleRate22050;
                break;
            case 44100:
                m_acodec->m_soundRate = AudioSampleRate44100;
                break;
            default:
                break;
        };
    }

    return err;
}

error Format::AudioMp3Demux(Buffer *stream, int64_t timestamp)
{
    error err = SUCCESS;

    m_audio->m_cts = 0;
    m_audio->m_dts = timestamp;
    m_audio->m_aacPacketType = AudioMp3FrameTrait;

    // @see: E.4.2 Audio Tags, video_file_format_spec_v10_1.pdf, page 76
    int8_t sound_format = stream->Read1Bytes();

    int8_t sound_type = sound_format & 0x01;
    int8_t sound_size = (sound_format >> 1) & 0x01;
    int8_t sound_rate = (sound_format >> 2) & 0x03;
    sound_format = (sound_format >> 4) & 0x0f;

    AudioCodecId codec_id = (AudioCodecId)sound_format;
    m_acodec->m_id = codec_id;

    m_acodec->m_soundType = (AudioChannels)sound_type;
    m_acodec->m_soundRate = (AudioSampleRate)sound_rate;
    m_acodec->m_soundSize = (AudioSampleBits)sound_size;

    // we always decode aac then mp3.
    Assert(m_acodec->m_id == AudioCodecIdMP3);

    // Update the RAW MP3 data.
    m_raw = stream->Data() + stream->Pos();
    m_nbRaw = stream->Size() - stream->Pos();

    stream->Skip(1);
    if (stream->Empty()) {
        return err;
    }

    char* data = stream->Data() + stream->Pos();
    int size = stream->Size() - stream->Pos();

    // mp3 payload.
    if ((err = m_audio->AddSample(data, size)) != SUCCESS) {
        return ERRORWRAP(err, "add audio frame");
    }

    return err;
}

error Format::AudioAacSequenceHeaderDemux(char *data, int size)
{
    error err = SUCCESS;

    Buffer* buffer = new Buffer(data, size);
    AutoFree(Buffer, buffer);

    // only need to decode the first 2bytes:
    //      audioObjectType, aac_profile, 5bits.
    //      samplingFrequencyIndex, aac_sample_rate, 4bits.
    //      channelConfiguration, aac_channels, 4bits
    if (!buffer->Require(2)) {
        return ERRORNEW(ERROR_HLS_DECODE_ERROR, "audio codec decode aac sh");
    }
    uint8_t profile_ObjectType = buffer->Read1Bytes();
    uint8_t samplingFrequencyIndex = buffer->Read1Bytes();

    m_acodec->m_aacChannels = (samplingFrequencyIndex >> 3) & 0x0f;
    samplingFrequencyIndex = ((profile_ObjectType << 1) & 0x0e) | ((samplingFrequencyIndex >> 7) & 0x01);
    profile_ObjectType = (profile_ObjectType >> 3) & 0x1f;

    // set the aac sample rate.
    m_acodec->m_aacSampleRate = samplingFrequencyIndex;

    // convert the object type in sequence header to aac profile of ADTS.
    m_acodec->m_aacObject = (AacObjectType)profile_ObjectType;
    if (m_acodec->m_aacObject == AacObjectTypeReserved) {
        return ERRORNEW(ERROR_HLS_DECODE_ERROR, "aac decode sh object %d", profile_ObjectType);
    }

    // TODO: FIXME: to support aac he/he-v2, see: ngx_rtmp_codec_parse_aac_header
    // @see: https://github.com/winlinvip/nginx-rtmp-module/commit/3a5f9eea78fc8d11e8be922aea9ac349b9dcbfc2
    //
    // donot force to LC, @see: https://github.com/ossrs/srs/issues/81
    // the source will print the sequence header info.
    //if (aac_profile > 3) {
        // Mark all extended profiles as LC
        // to make Android as happy as possible.
        // @see: ngx_rtmp_hls_parse_aac_header
        //aac_profile = 1;
    //}

    return err;
}
