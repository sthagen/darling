/*
This file is part of Darling.

Copyright (C) 2020 Lubos Dolezel

Darling is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

Darling is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with Darling.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "AudioConverterImpl.h"
#include <CoreServices/MacErrors.h>
#include <stdexcept>
#include <cstring>
#include <cassert>
#include <iostream>
#include <sstream>
#include "stub.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/opt.h>
#include <libavutil/mem.h>
}

static constexpr int ENCODER_FRAME_SAMPLES = 1024;

// http://blinkingblip.wordpress.com/

__attribute__((constructor)) static void init_avcodec()
{
	avcodec_register_all();
}

static void throwFFMPEGError(int errnum, const char* function)
{
	char buf[256];
	std::stringstream ss;

	if (av_strerror(errnum, buf, sizeof(buf)) == 0)
		ss << function << ": " << buf;
	else
		ss << function << ": unknown error";

	throw std::runtime_error(ss.str());
}

AudioConverter::AudioConverter(const AudioStreamBasicDescription* inSourceFormat, const AudioStreamBasicDescription* inDestinationFormat)
    : m_sourceFormat(*inSourceFormat), m_destinationFormat(*inDestinationFormat), m_decoder(nullptr), m_encoder(nullptr)
{
	memset(&m_avpkt, 0, sizeof(m_avpkt));
	memset(&m_avpktOut, 0, sizeof(m_avpktOut));
}

void AudioConverter::flush()
{
	TRACE();
	avcodec_flush_buffers(m_decoder);
	avcodec_flush_buffers(m_encoder);
	//avcodec_close(m_encoder);
}

OSStatus AudioConverter::create(const AudioStreamBasicDescription* inSourceFormat, const AudioStreamBasicDescription* inDestinationFormat, AudioConverter** out)
{
	TRACE2(inSourceFormat, inDestinationFormat);
	
	// TODO: non-interleaved audio

	AVCodec *codecIn, *codecOut;
	AVCodecContext *cIn;
	AVCodecContext *cOut;
	enum AVCodecID idIn, idOut;

	*out = nullptr;
	idIn = CACodecToAV(inSourceFormat);
	idOut = CACodecToAV(inDestinationFormat);

	if (idIn == AV_CODEC_ID_NONE || idOut == AV_CODEC_ID_NONE)
	{
		// LOG << "AudioConverter::create(): Unsupported codec, format in = " << std::hex << inSourceFormat->mFormatID << ", out = " << inDestinationFormat->mFormatID << std::dec << std::endl;
		return paramErr;
	}

	codecIn = avcodec_find_decoder(idIn);
	codecOut = avcodec_find_encoder(idOut);

	if (!codecIn || !codecOut)
	{
		// LOG << "AudioConverter::create(): avcodec_find_*() failed, format in = " << std::hex << inSourceFormat->mFormatID << ", out = " << inDestinationFormat->mFormatID << std::dec << std::endl;
		return paramErr;
	}

	*out = new AudioConverter(inSourceFormat, inDestinationFormat);

	(*out)->m_decoder = cIn = avcodec_alloc_context3(codecIn);
	
	if (inSourceFormat->mFormatID == kAudioFormatLinearPCM)
	{
		cIn->channels = inSourceFormat->mChannelsPerFrame;
		cIn->sample_rate = inSourceFormat->mSampleRate;
		
		std::cout << "Converting from PCM with " << cIn->channels << " channels at " << cIn->sample_rate << " Hz\n";
	}
	
	if (avcodec_open2((*out)->m_decoder, codecIn, nullptr) < 0)
	{
		delete *out;
		std::cerr << "AudioConverter::create(): avcodec_open() failed, format in = " << std::hex << inSourceFormat->mFormatID << ", out = " << inDestinationFormat->mFormatID << std::dec << std::endl;

		return paramErr;
	}

	// The encoder will be initialized after we process the first packet
	(*out)->m_encoder = cOut = avcodec_alloc_context3(codecOut);
	(*out)->m_codecIn = codecIn;
	(*out)->m_codecOut = codecOut;

	return noErr;
}

void AudioConverter::initEncoder()
{
	int err;
	
	if (m_encoderInitialized)
		throw std::logic_error("Encoder already initialized");
	
	m_encoder->codec_type = AVMEDIA_TYPE_AUDIO;
	m_encoder->bit_rate = m_outBitRate;
	m_encoder->channels = m_destinationFormat.mChannelsPerFrame;
	m_encoder->sample_rate = m_destinationFormat.mSampleRate;
	m_encoder->channel_layout = CAChannelCountToLayout(m_destinationFormat.mChannelsPerFrame);
	m_encoder->sample_fmt = CACodecSampleFormat(&m_destinationFormat);

#ifdef DEBUG_AUDIOCONVERTER
	std::cout << "ENCODER FORMAT:\n";
	std::cout << "\tSample rate: " << m_encoder->sample_rate << std::endl;
	std::cout << "\tChannels: " << m_destinationFormat.mChannelsPerFrame << std::endl;
	std::cout << "\tFormat: 0x" << std::hex << m_encoder->sample_fmt << std::dec << std::endl;
#endif
	
	err = avcodec_open2(m_encoder, m_codecOut, 0);
	if (err < 0)
		throwFFMPEGError(err, "avcodec_open2() encoder");
	
	allocateBuffers();
	m_encoderInitialized = true;
}

void AudioConverter::allocateBuffers()
{
#ifdef HAVE_AV_FRAME_ALLOC
	m_audioFrame = av_frame_alloc();
#else
	m_audioFrame = avcodec_alloc_frame();
#endif
	
	m_audioFrame->nb_samples = ENCODER_FRAME_SAMPLES;
	m_audioFrame->format = m_encoder->sample_fmt;
	m_audioFrame->channel_layout = m_encoder->channel_layout;

	
	int audioSampleBuffer_size = av_samples_get_buffer_size(nullptr, m_encoder->channels, m_audioFrame->nb_samples, m_encoder->sample_fmt, 0);
	void* audioSampleBuffer = (uint8_t*) av_malloc(audioSampleBuffer_size);

	if (!audioSampleBuffer)
	{
		std::cerr << "AudioConverter::allocateBuffers(): Failed to allocate sample buffer\n";
		throw std::runtime_error("AudioConverter::allocateBuffers(): Failed to allocate sample buffer");
	}

	// Setup the data pointers in the AVFrame
	if (int err = avcodec_fill_audio_frame(m_audioFrame, m_encoder->channels, m_encoder->sample_fmt,
		(const uint8_t*) audioSampleBuffer, audioSampleBuffer_size, 0 ); err < 0)
	{
		std::cerr << "AudioConverter::allocateBuffers(): Could not set up audio frame\n";
		throw std::runtime_error("AudioConverter::allocateBuffers(): Could not set up audio frame");
	}
}

AudioConverter::~AudioConverter()
{
	TRACE();
	if (m_decoder)
		avcodec_close(m_decoder);
	if (m_encoder)
		avcodec_close(m_encoder);
	if (m_audioFrame)
		av_free(m_audioFrame);
	if (m_resampler)
		avresample_free(&m_resampler);
}

template <typename T> OSStatus setPropertyT(UInt32 inPropertyDataSize, T* localProperty, const void* propertySource)
{
	const T* t = static_cast<const T*>(propertySource);
	
	if (inPropertyDataSize != sizeof(T))
		return kAudioConverterErr_BadPropertySizeError;
	
	*localProperty = *t;
	return noErr;
}

OSStatus AudioConverter::setProperty(AudioConverterPropertyID inPropertyID, UInt32 inPropertyDataSize, const void *inPropertyData)
{
	switch (inPropertyID)
	{
		case kAudioConverterEncodeBitRate:
		{
			return setPropertyT(inPropertyDataSize, &m_outBitRate, inPropertyData);
		}
		case kAudioConverterInputChannelLayout:
		{
		}
		case kAudioConverterOutputChannelLayout:
		{
		}
		case kAudioConverterCurrentOutputStreamDescription:
		{
			return kAudioConverterErr_PropertyNotSupported;
			//return setPropertyT(inPropertyDataSize, &m_sourceFormat, inPropertyData);
		}
		case kAudioConverterCurrentInputStreamDescription:
		{
			return kAudioConverterErr_PropertyNotSupported;
			//return setPropertyT(inPropertyDataSize, &m_destinationFormat, inPropertyData);
		}
		default:
		{
			STUB();
			return kAudioConverterErr_PropertyNotSupported;
		}
	}
	
	return unimpErr;
}

OSStatus AudioConverter::getPropertyInfo(AudioConverterPropertyID inPropertyID, UInt32 *outSize, Boolean *outWritable)
{
	STUB();
	return unimpErr;
}

template <typename T> OSStatus getPropertyT(UInt32 *ioPropertyDataSize, const T* localProperty, void* propertyTarget)
{
	T* t = static_cast<T*>(propertyTarget);
	
	if (*ioPropertyDataSize < sizeof(T))
		return kAudioConverterErr_BadPropertySizeError;
	*ioPropertyDataSize = sizeof(T);
	
	*t = *localProperty;
	return noErr;
}

OSStatus AudioConverter::getProperty(AudioConverterPropertyID inPropertyID, UInt32 *ioPropertyDataSize, void *outPropertyData)
{
	switch (inPropertyID)
	{
		case kAudioConverterEncodeBitRate:
		{
			return getPropertyT(ioPropertyDataSize, &m_outBitRate, outPropertyData);
		}
		case kAudioConverterCurrentInputStreamDescription:
		{
			return getPropertyT(ioPropertyDataSize, &m_sourceFormat, outPropertyData);
		}
		case kAudioConverterCurrentOutputStreamDescription:
		{
			return getPropertyT(ioPropertyDataSize, &m_destinationFormat, outPropertyData);
		}
		default:
		{
			STUB();
			return kAudioConverterErr_PropertyNotSupported;
		}
	}
}

OSStatus AudioConverter::feedInput(AudioConverterComplexInputDataProc dataProc, void* opaque, UInt32& numDataPackets)
{
	AudioBufferList bufferList;
	AudioStreamPacketDescription* aspd;
	OSStatus err;

	numDataPackets = 4096; // TODO: increase this?
	bufferList.mNumberBuffers = 1;
	bufferList.mBuffers[0].mDataByteSize = 0;
	bufferList.mBuffers[0].mData = nullptr;
	
	err = dataProc(AudioConverterRef(this), &numDataPackets, &bufferList, &aspd, opaque);
	
	if (err != noErr)
		return err;
	
	m_avpkt.size = bufferList.mBuffers[0].mDataByteSize;
	m_avpkt.data = (uint8_t*) bufferList.mBuffers[0].mData;
	
	return noErr;
}

void AudioConverter::setupResampler(const AVFrame* frame)
{
	int err;
	
	if (m_resampler != nullptr)
		throw std::logic_error("Resampler already created");
	
	m_resampler = avresample_alloc_context();
	m_targetFormat = CACodecSampleFormat(&m_destinationFormat);
	
	av_opt_set_int(m_resampler, "in_channel_layout", CAChannelCountToLayout(m_sourceFormat.mChannelsPerFrame), 0);
	av_opt_set_int(m_resampler, "out_channel_layout", CAChannelCountToLayout(m_destinationFormat.mChannelsPerFrame), 0);
	av_opt_set_int(m_resampler, "in_channels", m_sourceFormat.mChannelsPerFrame, 0);
	av_opt_set_int(m_resampler, "out_channels", m_destinationFormat.mChannelsPerFrame, 0);
	av_opt_set_int(m_resampler, "in_sample_rate", frame->sample_rate, 0);
	av_opt_set_int(m_resampler, "out_sample_rate", m_destinationFormat.mSampleRate, 0);
	av_opt_set_int(m_resampler, "in_sample_fmt", frame->format, 0);
	av_opt_set_int(m_resampler, "out_sample_fmt", m_targetFormat, 0);

#ifdef DEBUG_AUDIOCONVERTER
	std::cout << "RESAMPLER:\n";
	std::cout << "\tInput rate: " << frame->sample_rate << std::endl;
	std::cout << "\tInput format: 0x" << std::hex << frame->format << std::dec <<std::endl;
	std::cout << "\tOutput rate: " << m_destinationFormat.mSampleRate << std::endl;
	std::cout << "\tOutput format: 0x" << std::hex << CACodecSampleFormat(&m_destinationFormat) << std::dec << std::endl;

	m_resamplerInput.open("/tmp/resampler.in.raw", std::ios_base::binary | std::ios_base::out);
	m_resamplerOutput.open("/tmp/resampler.out.raw", std::ios_base::binary | std::ios_base::out);
	m_encoderOutput.open("/tmp/encoder.out.raw", std::ios_base::binary | std::ios_base::out);
#endif
	
	err = avresample_open(m_resampler);
	if (err < 0)
		throwFFMPEGError(err, "avresample_open()");
}

OSStatus AudioConverter::fillComplex(AudioConverterComplexInputDataProc dataProc, void* opaque,
	UInt32* ioOutputDataPacketSize, AudioBufferList *outOutputData, AudioStreamPacketDescription* outPacketDescription)
{
	AVFrame* srcaudio;

#ifdef HAVE_AV_FRAME_ALLOC
	srcaudio = av_frame_alloc();
	av_frame_unref(srcaudio);
#else
	srcaudio = avcodec_alloc_frame();
	avcodec_get_frame_defaults(srcaudio);
#endif
	
	try
	{
		for (uint32_t i = 0; i < outOutputData->mNumberBuffers; i++)
		{
			UInt32 origSize = outOutputData->mBuffers[i].mDataByteSize;
			UInt32& newSize = outOutputData->mBuffers[i].mDataByteSize;
			
			newSize = 0;
			
			while (newSize < origSize)
			{
				if (m_avpktOutUsed < m_avpktOut.size)
				{
					// std::cout << "case 1 (used " << m_avpktOutUsed << " from " << m_avpktOut.size << ")\n";
					// Feed output from previous conversion
					while (m_avpktOutUsed < m_avpktOut.size && newSize < origSize)
					{
						// Output data
						int tocopy = std::min<int>(m_avpktOut.size - m_avpktOutUsed, origSize - newSize);
						memcpy(((char*) outOutputData->mBuffers[i].mData) + newSize, m_avpktOut.data + m_avpktOutUsed, tocopy);
						newSize += tocopy;
						m_avpktOutUsed += tocopy;
					}
					
					if (m_avpktOutUsed >= m_avpktOut.size)
					{
						m_avpktOutUsed = 0;
						av_free_packet(&m_avpktOut);
					}
				}
				else
				{
					while (!feedEncoder())
					{
						if (!feedDecoder(dataProc, opaque, srcaudio))
							goto end;
					}
				}
			}
		}
end:
		
#ifdef HAVE_AV_FRAME_ALLOC
		av_frame_free(&srcaudio);
#else
		avcodec_free_frame(&srcaudio);
#endif
	}
	catch (const std::exception& e)
	{
		std::cerr << "AudioConverter::fillComplex(): Exception: " << e.what();
#ifdef HAVE_AV_FRAME_ALLOC
		av_frame_free(&srcaudio);
#else
		avcodec_free_frame(&srcaudio);
#endif
		return ioErr;
	}
	catch (OSStatus err)
	{
		std::cerr << "AudioConverter::fillComplex(): OSStatus error: " << err;
#ifdef HAVE_AV_FRAME_ALLOC
		av_frame_free(&srcaudio);
#else
		avcodec_free_frame(&srcaudio);
#endif
		return err;
	}
	
	return noErr;
}

bool AudioConverter::feedDecoder(AudioConverterComplexInputDataProc dataProc, void* opaque, AVFrame* srcaudio)
{
	int gotFrame, err;
	
	do
	{
		// Read input
		if (!m_avpkt.size)
		{
			UInt32 numDataPackets = 0;
			OSStatus err = feedInput(dataProc, opaque, numDataPackets);

			// The documentation says that this may be a temporary condition
			if (err != noErr)
				return false;

			if (!m_avpkt.size) // numDataPackets cannot be trusted
				return false;
		}

		err = avcodec_decode_audio4(m_decoder, srcaudio, &gotFrame, &m_avpkt);
		if (err < 0)
			throwFFMPEGError(err, "avcodec_decode_audio4()");

		m_avpkt.size -= err;
		m_avpkt.data += err;

		if (gotFrame)
		{
			if (!m_resampler)
				setupResampler(srcaudio);

#ifdef DEBUG_AUDIOCONVERTER
			m_resamplerInput.write((char*) srcaudio->data, srcaudio->nb_samples * 2 * m_sourceFormat.mChannelsPerFrame);
			m_resamplerInput.flush();
#endif
			
			// Resample PCM
			err = avresample_convert(m_resampler, nullptr, 0, 0, srcaudio->data, 0, srcaudio->nb_samples);
			if (err < 0)
				throwFFMPEGError(err, "avresample_convert()");
		}
	}
	while (!gotFrame);

	return true;
}

bool AudioConverter::feedEncoder()
{
	int gotFrame = 0, err;
	uint8_t *output;
	int out_linesize;
	int avail;

	if (!m_resampler)
		return false;

	if (!m_encoderInitialized)
		initEncoder();
	
	assert(m_avpktOutUsed == m_avpktOut.size);

	const size_t bytesPerSample = m_destinationFormat.mBitsPerChannel / 8;
	const size_t bytesPerFrame = m_destinationFormat.mChannelsPerFrame * bytesPerSample;
	const size_t requiredBytes = bytesPerFrame * ENCODER_FRAME_SAMPLES;

	while (m_audioFramePrebuf.size() < requiredBytes && (avail = avresample_available(m_resampler)) > 0)
	{
		av_samples_alloc(&output, &out_linesize, m_destinationFormat.mChannelsPerFrame,
			avail, m_encoder->sample_fmt, 0);

		if (avresample_read(m_resampler, &output, avail) != avail)
		{
			av_freep(&output);
			throwFFMPEGError(err, "avresample_read()");
		}

#ifdef DEBUG_AUDIOCONVERTER
		m_resamplerOutput.write((char*) output, avail * bytesPerFrame);
		m_resamplerOutput.flush();
#endif

		m_audioFramePrebuf.push(output, avail * bytesPerFrame);
		av_freep(&output);
	}

	av_init_packet(&m_avpktOut);
	m_avpktOut.data = 0;
	m_avpktOut.size = 0;
	m_avpktOutUsed = 0;

	if (m_audioFramePrebuf.size() >= requiredBytes)
	{
		try
		{
			err = avcodec_fill_audio_frame(m_audioFrame, m_destinationFormat.mChannelsPerFrame,
					m_targetFormat, m_audioFramePrebuf.data(), requiredBytes, 0);
			
			if (err < 0)
				throwFFMPEGError(err, "avcodec_fill_audio_frame()");

			err = avcodec_encode_audio2(m_encoder, &m_avpktOut, m_audioFrame, &gotFrame);
			if (err < 0)
				throwFFMPEGError(err, "avcodec_encode_audio2()");

			m_audioFramePrebuf.consume(requiredBytes);

#ifdef DEBUG_AUDIOCONVERTER
			if (gotFrame)
				m_encoderOutput.write((char*) m_avpktOut.data, m_avpktOut.size);
#endif
		}
		catch (...)
		{
			m_audioFramePrebuf.consume(requiredBytes);
			throw;
		}

		return gotFrame;
	}
	return false;
}

uint32_t AudioConverter::CAChannelCountToLayout(UInt32 numChannels)
{
	// TODO: this is just wild guessing
	switch (numChannels)
	{
		case 1:
			return AV_CH_LAYOUT_MONO;
		case 2:
			return AV_CH_LAYOUT_STEREO;
		case 3:
			return AV_CH_LAYOUT_SURROUND;
		case 4:
			return AV_CH_LAYOUT_4POINT0;
		case 5:
			return AV_CH_LAYOUT_4POINT1;
		case 6:
			return AV_CH_LAYOUT_5POINT1;
		case 7:
			return AV_CH_LAYOUT_6POINT1;
		case 8:
			return AV_CH_LAYOUT_7POINT1;
		default:
			return AV_CH_LAYOUT_STEREO;
	}
}

enum AVSampleFormat AudioConverter::CACodecSampleFormat(const AudioStreamBasicDescription* desc)
{
	if (desc->mFormatFlags & kAudioFormatFlagIsFloat)
	{
		if (desc->mBitsPerChannel == 32)
			return AV_SAMPLE_FMT_FLT;
		else if (desc->mBitsPerChannel == 64)
			return AV_SAMPLE_FMT_DBL;
		else
			return AV_SAMPLE_FMT_NONE;
	}
	else
	{
		
		switch (desc->mBitsPerChannel)
		{
			case 8: return AV_SAMPLE_FMT_U8;
			case 16: return AV_SAMPLE_FMT_S16;
			case 24: return AV_SAMPLE_FMT_S32; // FIXME: 24-bits?
			case 32: return AV_SAMPLE_FMT_S32;
			default: return AV_SAMPLE_FMT_NONE;
		}
	}
}

enum AVCodecID AudioConverter::CACodecToAV(const AudioStreamBasicDescription* desc)
{
	switch (desc->mFormatID)
	{
		case kAudioFormatLinearPCM:
		{
			if (desc->mFormatFlags & kAudioFormatFlagIsFloat)
			{
				if (desc->mFormatFlags & kAudioFormatFlagIsBigEndian)
				{
					if (desc->mBitsPerChannel == 32)
						return AV_CODEC_ID_PCM_F32BE;
					else if (desc->mBitsPerChannel == 64)
						return AV_CODEC_ID_PCM_F64BE;
				}
				else
				{
					if (desc->mBitsPerChannel == 32)
						return AV_CODEC_ID_PCM_F32LE;
					else if (desc->mBitsPerChannel == 64)
						return AV_CODEC_ID_PCM_F64LE;
				}
			}
			else if (desc->mFormatFlags & kAudioFormatFlagIsSignedInteger)
			{
				enum AVCodecID cid;

				switch (desc->mBitsPerChannel)
				{
					case 8: cid = AV_CODEC_ID_PCM_S8; break;
					case 16: cid = AV_CODEC_ID_PCM_S16LE; break;
					case 24: cid = AV_CODEC_ID_PCM_S24LE; break;
					case 32: cid = AV_CODEC_ID_PCM_S32LE; break;
					default: return AV_CODEC_ID_NONE;
				}

				if (desc->mBitsPerChannel != 8 && desc->mFormatFlags & kAudioFormatFlagIsBigEndian)
					cid = (enum AVCodecID)(int(cid)+1);
				return cid;
			}
			else
			{
				enum AVCodecID cid;

				switch (desc->mBitsPerChannel)
				{
					case 8: cid = AV_CODEC_ID_PCM_U8; break;
					case 16: cid = AV_CODEC_ID_PCM_U16LE; break;
					case 24: cid = AV_CODEC_ID_PCM_U24LE; break;
					case 32: cid = AV_CODEC_ID_PCM_U32LE; break;
					default: return AV_CODEC_ID_NONE;
				}

				if (desc->mBitsPerChannel != 8 && desc->mFormatFlags & kAudioFormatFlagIsBigEndian)
					cid = (enum AVCodecID)(int(cid)+1);
				return cid;
			}
			return AV_CODEC_ID_NONE;
		}
		case kAudioFormatULaw:
			return AV_CODEC_ID_PCM_MULAW;
		case kAudioFormatALaw:
			return AV_CODEC_ID_PCM_ALAW;
		case kAudioFormatMPEGLayer1:
			return AV_CODEC_ID_MP1;
		case kAudioFormatMPEGLayer2:
			return AV_CODEC_ID_MP2;
		case kAudioFormatMPEGLayer3:
			return AV_CODEC_ID_MP3;
		case kAudioFormatAC3:
		case kAudioFormat60958AC3: // TODO: is this correct?
			return AV_CODEC_ID_AC3;
		case kAudioFormatAppleIMA4:
			return AV_CODEC_ID_ADPCM_IMA_DK4; // TODO: is this correct?
		case kAudioFormatMPEG4AAC:
		case kAudioFormatMPEG4AAC_HE:
		case kAudioFormatMPEG4AAC_LD:
		case kAudioFormatMPEG4AAC_ELD:
		case kAudioFormatMPEG4AAC_ELD_SBR:
		case kAudioFormatMPEG4AAC_ELD_V2:
		case kAudioFormatMPEG4AAC_HE_V2:
		case kAudioFormatMPEG4AAC_Spatial:
			return AV_CODEC_ID_AAC;
		case kAudioFormatMPEG4CELP:
			return AV_CODEC_ID_QCELP; // TODO: is this correct?
		case kAudioFormatAMR:
			return AV_CODEC_ID_AMR_NB;
		case kAudioFormatiLBC:
			return AV_CODEC_ID_ILBC;
		case kAudioFormatAppleLossless:
			return AV_CODEC_ID_APE;
		case kAudioFormatMicrosoftGSM:
			return AV_CODEC_ID_GSM_MS;
		case kAudioFormatMACE3:
			return AV_CODEC_ID_MACE3;
		case kAudioFormatMACE6:
			return AV_CODEC_ID_MACE6;
		default:
			return AV_CODEC_ID_NONE;
	}
}

