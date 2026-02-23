/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "ocean/media/mediafoundation/VideoDecoder.h"

#include <mferror.h>

namespace Ocean
{

namespace Media
{

namespace MediaFoundation
{

VideoDecoder::VideoDecoder()
{
	// nothing to do here
}

VideoDecoder::~VideoDecoder()
{
	release();
}

bool VideoDecoder::initialize(const std::string& mime, const unsigned int width, const unsigned int height, const void* codecConfigData, const size_t codecConfigSize)
{
	ocean_assert(!mime.empty());
	ocean_assert(width != 0u && height != 0u);

	if (mime.empty() || width == 0u || height == 0u)
	{
		return false;
	}

	const ScopedLock scopedLock(lock_);

	if (decoder_.isValid())
	{
		ocean_assert(false && "Already initialized");
		return false;
	}

	const HRESULT startupResult = MFStartup(MF_VERSION);

	if (S_OK != startupResult)
	{
		Log::error() << "VideoDecoder: Failed to initialize Media Foundation, error: 0x" << String::toAStringHex(int(startupResult));
		return false;
	}

	mfStarted_ = true;

	const GUID videoFormat = mimeToVideoFormat(mime);

	if (videoFormat == GUID_NULL)
	{
		Log::error() << "VideoDecoder: Unsupported MIME type: " << mime;
		return false;
	}

	MFT_REGISTER_TYPE_INFO inputTypeInfo = {MFMediaType_Video, videoFormat};

	IMFActivate** ppActivate = nullptr;
	UINT32 activateCount = 0;

	const UINT32 enumFlags = MFT_ENUM_FLAG_SYNCMFT | MFT_ENUM_FLAG_LOCALMFT | MFT_ENUM_FLAG_SORTANDFILTER;

	if (S_OK != MFTEnumEx(MFT_CATEGORY_VIDEO_DECODER, enumFlags, &inputTypeInfo, nullptr, &ppActivate, &activateCount) || activateCount == 0u)
	{
		Log::error() << "VideoDecoder: No decoder found for MIME type: " << mime;

		if (ppActivate != nullptr)
		{
			CoTaskMemFree(ppActivate);
		}

		return false;
	}

	ScopedIMFMediaType inputType;

	if (S_OK != MFCreateMediaType(&inputType.resetObject()))
	{
		for (UINT32 i = 0u; i < activateCount; ++i)
		{
			ppActivate[i]->Release();
		}

		CoTaskMemFree(ppActivate);
		return false;
	}

	inputType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
	inputType->SetGUID(MF_MT_SUBTYPE, videoFormat);
	MFSetAttributeSize(*inputType, MF_MT_FRAME_SIZE, width, height);

	if (codecConfigData != nullptr && codecConfigSize > 0)
	{
		inputType->SetBlob(MF_MT_MPEG_SEQUENCE_HEADER, static_cast<const UINT8*>(codecConfigData), UINT32(codecConfigSize));
	}

	bool decoderCreated = false;

	for (UINT32 i = 0u; i < activateCount; ++i)
	{
		ScopedIMFTransform decoder;

		if (S_OK != ppActivate[i]->ActivateObject(IID_PPV_ARGS(&decoder.resetObject())) || !decoder.isValid())
		{
			continue;
		}

		if (S_OK != decoder->SetInputType(0, *inputType, 0))
		{
			continue;
		}

		bool outputTypeSet = false;

		for (DWORD typeIndex = 0u; ; ++typeIndex)
		{
			ScopedIMFMediaType availableOutputType;

			if (S_OK != decoder->GetOutputAvailableType(0, typeIndex, &availableOutputType.resetObject()))
			{
				break;
			}

			GUID subtype = GUID_NULL;
			availableOutputType->GetGUID(MF_MT_SUBTYPE, &subtype);

			if (subtype == MFVideoFormat_NV12)
			{
				if (S_OK == decoder->SetOutputType(0, *availableOutputType, 0))
				{
					outputTypeSet = true;
					break;
				}
			}
		}

		if (!outputTypeSet)
		{
			ScopedIMFMediaType fallbackType;

			if (S_OK == decoder->GetOutputAvailableType(0, 0, &fallbackType.resetObject()))
			{
				outputTypeSet = (S_OK == decoder->SetOutputType(0, *fallbackType, 0));
			}
		}

		if (!outputTypeSet)
		{
			continue;
		}

		decoder_ = std::move(decoder);
		decoderCreated = true;
		break;
	}

	for (UINT32 i = 0u; i < activateCount; ++i)
	{
		ppActivate[i]->Release();
	}

	CoTaskMemFree(ppActivate);

	if (!decoderCreated)
	{
		Log::error() << "VideoDecoder: Failed to create and configure decoder for MIME type: " << mime;
		return false;
	}

	MFT_OUTPUT_STREAM_INFO outputStreamInfo;

	if (S_OK == decoder_->GetOutputStreamInfo(0, &outputStreamInfo))
	{
		mftProvidesOutputSamples_ = (outputStreamInfo.dwFlags & (MFT_OUTPUT_STREAM_PROVIDES_SAMPLES | MFT_OUTPUT_STREAM_LAZY_READ)) != 0;
		outputBufferSize_ = outputStreamInfo.cbSize;
	}

	width_ = width;
	height_ = height;

	ocean_assert(isStarted_ == false);

	return true;
}

bool VideoDecoder::start()
{
	const ScopedLock scopedLock(lock_);

	if (!decoder_.isValid())
	{
		ocean_assert(false && "Not initialized");
		return false;
	}

	if (isStarted_)
	{
		return true;
	}

	const HRESULT beginStreamingResult = decoder_->ProcessMessage(MFT_MESSAGE_NOTIFY_BEGIN_STREAMING, 0);

	if (S_OK != beginStreamingResult)
	{
		Log::error() << "VideoDecoder: Failed to begin streaming, error: 0x" << String::toAStringHex(int(beginStreamingResult));
		return false;
	}

	const HRESULT startStreamResult = decoder_->ProcessMessage(MFT_MESSAGE_NOTIFY_START_OF_STREAM, 0);

	if (S_OK != startStreamResult)
	{
		Log::error() << "VideoDecoder: Failed to start stream, error: 0x" << String::toAStringHex(int(startStreamResult));
		return false;
	}

	isStarted_ = true;

	return true;
}

bool VideoDecoder::stop()
{
	const ScopedLock scopedLock(lock_);

	if (!decoder_.isValid() || !isStarted_)
	{
		return true;
	}

	// Use DRAIN to flush out remaining frames, not FLUSH which discards them
	decoder_->ProcessMessage(MFT_MESSAGE_COMMAND_DRAIN, 0);
	decoder_->ProcessMessage(MFT_MESSAGE_NOTIFY_END_OF_STREAM, 0);
	decoder_->ProcessMessage(MFT_MESSAGE_NOTIFY_END_STREAMING, 0);

	isStarted_ = false;

	return true;
}

bool VideoDecoder::pushSample(const void* data, const size_t size, const uint64_t presentationTime)
{
	ocean_assert(data != nullptr && size != 0);

	if (data == nullptr || size == 0)
	{
		return false;
	}

	const ScopedLock scopedLock(lock_);

	if (!decoder_.isValid())
	{
		ocean_assert(false && "Not initialized");
		return false;
	}

	if (!isStarted_)
	{
		ocean_assert(false && "Not started");
		return false;
	}

	ScopedIMFMediaBuffer mediaBuffer;

	if (S_OK != MFCreateMemoryBuffer(DWORD(size), &mediaBuffer.resetObject()))
	{
		Log::error() << "VideoDecoder: Failed to create media buffer";
		return false;
	}

	BYTE* bufferData = nullptr;

	if (S_OK != mediaBuffer->Lock(&bufferData, nullptr, nullptr) || bufferData == nullptr)
	{
		Log::error() << "VideoDecoder: Failed to lock media buffer";
		return false;
	}

	memcpy(bufferData, data, size);
	mediaBuffer->Unlock();
	mediaBuffer->SetCurrentLength(DWORD(size));

	ScopedIMFSample inputSample;

	if (S_OK != MFCreateSample(&inputSample.resetObject()))
	{
		Log::error() << "VideoDecoder: Failed to create sample";
		return false;
	}

	inputSample->AddBuffer(*mediaBuffer);
	inputSample->SetSampleTime(int64_t(presentationTime) * 10);

	const HRESULT processInputResult = decoder_->ProcessInput(0, *inputSample, 0);

	if (processInputResult == MF_E_NOTACCEPTING)
	{
		return false;
	}

	if (S_OK != processInputResult)
	{
		Log::error() << "VideoDecoder: ProcessInput failed, error: 0x" << String::toAStringHex(int(processInputResult));
		return false;
	}

	return true;
}

Frame VideoDecoder::popFrame(int64_t* presentationTime)
{
	const ScopedLock scopedLock(lock_);

	if (!decoder_.isValid())
	{
		ocean_assert(false && "Not initialized");
		return Frame();
	}

	MFT_OUTPUT_DATA_BUFFER outputDataBuffer = {};
	outputDataBuffer.dwStreamID = 0;
	outputDataBuffer.pSample = nullptr;
	outputDataBuffer.dwStatus = 0;
	outputDataBuffer.pEvents = nullptr;

	ScopedIMFSample callerProvidedSample;

	if (!mftProvidesOutputSamples_)
	{
		if (S_OK != MFCreateSample(&callerProvidedSample.resetObject()))
		{
			return Frame();
		}

		DWORD bufferSize = outputBufferSize_;

		if (bufferSize == 0u)
		{
			bufferSize = width_ * height_ * 3u / 2u;
		}

		ScopedIMFMediaBuffer outputBuffer;

		if (S_OK != MFCreateMemoryBuffer(bufferSize, &outputBuffer.resetObject()))
		{
			return Frame();
		}

		callerProvidedSample->AddBuffer(*outputBuffer);
		outputDataBuffer.pSample = *callerProvidedSample;
	}

	DWORD processOutputStatus = 0;
	const HRESULT processOutputResult = decoder_->ProcessOutput(0, 1, &outputDataBuffer, &processOutputStatus);

	if (processOutputResult == MF_E_TRANSFORM_NEED_MORE_INPUT)
	{
		if (outputDataBuffer.pEvents != nullptr)
		{
			outputDataBuffer.pEvents->Release();
		}

		return Frame();
	}

	if (processOutputResult == MF_E_TRANSFORM_STREAM_CHANGE)
	{
		for (DWORD typeIndex = 0u; ; ++typeIndex)
		{
			ScopedIMFMediaType availableType;

			if (S_OK != decoder_->GetOutputAvailableType(0, typeIndex, &availableType.resetObject()))
			{
				break;
			}

			GUID subtype = GUID_NULL;
			availableType->GetGUID(MF_MT_SUBTYPE, &subtype);

			if (subtype == MFVideoFormat_NV12)
			{
				decoder_->SetOutputType(0, *availableType, 0);
				break;
			}
		}

		MFT_OUTPUT_STREAM_INFO streamInfo;

		if (S_OK == decoder_->GetOutputStreamInfo(0, &streamInfo))
		{
			mftProvidesOutputSamples_ = (streamInfo.dwFlags & (MFT_OUTPUT_STREAM_PROVIDES_SAMPLES | MFT_OUTPUT_STREAM_LAZY_READ)) != 0;
			outputBufferSize_ = streamInfo.cbSize;
		}

		if (outputDataBuffer.pEvents != nullptr)
		{
			outputDataBuffer.pEvents->Release();
		}

		return Frame();
	}

	if (S_OK != processOutputResult)
	{
		Log::error() << "VideoDecoder: ProcessOutput failed, error: 0x" << String::toAStringHex(int(processOutputResult));

		if (mftProvidesOutputSamples_ && outputDataBuffer.pSample != nullptr)
		{
			outputDataBuffer.pSample->Release();
		}

		if (outputDataBuffer.pEvents != nullptr)
		{
			outputDataBuffer.pEvents->Release();
		}

		return Frame();
	}

	IMFSample* outputSample = outputDataBuffer.pSample;
	Frame frame;

	if (outputSample != nullptr)
	{
		ScopedIMFMediaType currentOutputType;
		decoder_->GetOutputCurrentType(0, &currentOutputType.resetObject());

		UINT32 outputWidth = width_;
		UINT32 outputHeight = height_;

		if (currentOutputType.isValid())
		{
			MFGetAttributeSize(*currentOutputType, MF_MT_FRAME_SIZE, &outputWidth, &outputHeight);
		}

		ScopedIMFMediaBuffer mediaBuffer;

		if (S_OK == outputSample->ConvertToContiguousBuffer(&mediaBuffer.resetObject()))
		{
			IMF2DBuffer* buffer2D = nullptr;

			if (S_OK == mediaBuffer->QueryInterface(IID_PPV_ARGS(&buffer2D)) && buffer2D != nullptr)
			{
				BYTE* scanline0 = nullptr;
				LONG stride = 0;

				if (S_OK == buffer2D->Lock2D(&scanline0, &stride) && scanline0 != nullptr)
				{
					const unsigned int absStride = (unsigned int)(stride < 0 ? -stride : stride);

					unsigned int paddingElements = 0u;

					if (Frame::strideBytes2paddingElements(FrameType::FORMAT_Y_UV12_LIMITED_RANGE, outputWidth, absStride, paddingElements, 0u /*planeIndex*/))
					{
						const FrameType frameType(outputWidth, outputHeight, FrameType::FORMAT_Y_UV12_LIMITED_RANGE, FrameType::ORIGIN_UPPER_LEFT);

						const BYTE* yPlane = scanline0;
						const BYTE* uvPlane = scanline0 + size_t(absStride) * size_t(outputHeight);

						Frame::PlaneInitializers<uint8_t> planeInitializers =
						{
							Frame::PlaneInitializer<uint8_t>(yPlane, Frame::CM_COPY_REMOVE_PADDING_LAYOUT, paddingElements),
							Frame::PlaneInitializer<uint8_t>(uvPlane, Frame::CM_COPY_REMOVE_PADDING_LAYOUT, paddingElements)
						};

						frame = Frame(frameType, planeInitializers, Timestamp(true));
					}

					buffer2D->Unlock2D();
				}

				buffer2D->Release();
			}
			else
			{
				BYTE* bufferData = nullptr;
				DWORD bufferLength = 0;

				if (S_OK == mediaBuffer->Lock(&bufferData, nullptr, &bufferLength) && bufferData != nullptr)
				{
					const DWORD expectedSize = outputWidth * outputHeight * 3u / 2u;

					if (bufferLength >= expectedSize)
					{
						const FrameType frameType(outputWidth, outputHeight, FrameType::FORMAT_Y_UV12_LIMITED_RANGE, FrameType::ORIGIN_UPPER_LEFT);

						Frame::PlaneInitializers<uint8_t> planeInitializers =
						{
							Frame::PlaneInitializer<uint8_t>(bufferData, Frame::CM_COPY_REMOVE_PADDING_LAYOUT, 0u),
							Frame::PlaneInitializer<uint8_t>(bufferData + outputWidth * outputHeight, Frame::CM_COPY_REMOVE_PADDING_LAYOUT, 0u)
						};

						frame = Frame(frameType, planeInitializers, Timestamp(true));
					}

					mediaBuffer->Unlock();
				}
			}
		}

		if (presentationTime != nullptr)
		{
			LONGLONG sampleTime = 0;

			if (S_OK == outputSample->GetSampleTime(&sampleTime))
			{
				*presentationTime = sampleTime / 10;
			}
		}

		if (frame.isValid())
		{
			const Timestamp relativeTimestamp(Timestamp::microseconds2seconds(presentationTime != nullptr ? *presentationTime : 0));
			frame.setRelativeTimestamp(relativeTimestamp);
		}
	}

	if (mftProvidesOutputSamples_ && outputDataBuffer.pSample != nullptr)
	{
		outputDataBuffer.pSample->Release();
	}

	if (outputDataBuffer.pEvents != nullptr)
	{
		outputDataBuffer.pEvents->Release();
	}

	return frame;
}

bool VideoDecoder::drain()
{
	const ScopedLock scopedLock(lock_);

	if (!decoder_.isValid())
	{
		return false;
	}

	const HRESULT drainResult = decoder_->ProcessMessage(MFT_MESSAGE_COMMAND_DRAIN, 0);

	return S_OK == drainResult;
}

void VideoDecoder::release()
{
	const ScopedLock scopedLock(lock_);

	if (decoder_.isValid())
	{
		if (isStarted_)
		{
			stop();
		}

		decoder_.release();
	}

	width_ = 0u;
	height_ = 0u;
	mftProvidesOutputSamples_ = false;
	outputBufferSize_ = 0u;

	if (mfStarted_)
	{
		MFShutdown();
		mfStarted_ = false;
	}
}

bool VideoDecoder::convertAvccToAnnexB(const void* avccData, const size_t avccSize, std::vector<uint8_t>& annexBData, const bool isCodecConfig, const std::string& mime)
{
	ocean_assert(avccData != nullptr && avccSize >= 4);

	if (avccData == nullptr || avccSize < 4)
	{
		return false;
	}

	const uint8_t* data = static_cast<const uint8_t*>(avccData);
	static const uint8_t startCode[] = {0x00, 0x00, 0x00, 0x01};

	if (!isCodecConfig)
	{
		annexBData.clear();
		annexBData.reserve(avccSize);

		size_t offset = 0;

		while (offset + 4 <= avccSize)
		{
			const uint32_t nalLength = (uint32_t(data[offset]) << 24) | (uint32_t(data[offset + 1]) << 16) | (uint32_t(data[offset + 2]) << 8) | uint32_t(data[offset + 3]);
			offset += 4;

			if (nalLength == 0 || offset + nalLength > avccSize)
			{
				break;
			}

			annexBData.insert(annexBData.end(), startCode, startCode + 4);
			annexBData.insert(annexBData.end(), data + offset, data + offset + nalLength);

			offset += nalLength;
		}

		return !annexBData.empty();
	}

	const bool isHevc = (mime == "video/hevc" || mime == "video/h265");

	if (isHevc)
	{
		if (avccSize < 23)
		{
			return false;
		}

		annexBData.clear();

		const uint8_t numArrays = data[22];
		size_t offset = 23;

		for (uint8_t arrayIdx = 0; arrayIdx < numArrays && offset + 3 <= avccSize; ++arrayIdx)
		{
			offset++;
			const uint16_t numNalus = (uint16_t(data[offset]) << 8) | data[offset + 1];
			offset += 2;

			for (uint16_t naluIdx = 0; naluIdx < numNalus && offset + 2 <= avccSize; ++naluIdx)
			{
				const size_t naluLength = (size_t(data[offset]) << 8) | data[offset + 1];
				offset += 2;

				if (offset + naluLength > avccSize)
				{
					break;
				}

				annexBData.insert(annexBData.end(), startCode, startCode + 4);
				annexBData.insert(annexBData.end(), data + offset, data + offset + naluLength);

				offset += naluLength;
			}
		}

		return !annexBData.empty();
	}

	if (avccSize < 7)
	{
		return false;
	}

	annexBData.clear();

	size_t offset = 5;
	const uint8_t numSPS = data[offset] & 0x1F;
	offset++;

	for (uint8_t i = 0; i < numSPS && offset + 2 <= avccSize; ++i)
	{
		const size_t spsLength = (size_t(data[offset]) << 8) | data[offset + 1];
		offset += 2;

		if (offset + spsLength > avccSize)
		{
			break;
		}

		annexBData.insert(annexBData.end(), startCode, startCode + 4);
		annexBData.insert(annexBData.end(), data + offset, data + offset + spsLength);

		offset += spsLength;
	}

	if (offset < avccSize)
	{
		const uint8_t numPPS = data[offset];
		offset++;

		for (uint8_t i = 0; i < numPPS && offset + 2 <= avccSize; ++i)
		{
			const size_t ppsLength = (size_t(data[offset]) << 8) | data[offset + 1];
			offset += 2;

			if (offset + ppsLength > avccSize)
			{
				break;
			}

			annexBData.insert(annexBData.end(), startCode, startCode + 4);
			annexBData.insert(annexBData.end(), data + offset, data + offset + ppsLength);

			offset += ppsLength;
		}
	}

	return !annexBData.empty();
}

bool VideoDecoder::isAvcc(const void* data, const size_t size, const bool isCodecConfig)
{
	ocean_assert(data != nullptr && size >= 4);

	if (data == nullptr || size < 4)
	{
		return false;
	}

	const uint8_t* byteData = static_cast<const uint8_t*>(data);

	if (isCodecConfig)
	{
		// AVCC config starts with 0x01 (configurationVersion), Annex B starts with start codes
		if (byteData[0] == 0x00 && byteData[1] == 0x00 && byteData[2] == 0x00 && byteData[3] == 0x01)
		{
			return false;
		}

		if (byteData[0] == 0x00 && byteData[1] == 0x00 && byteData[2] == 0x01)
		{
			return false;
		}

		return true;
	}

	// For NAL samples: 0x00 0x00 0x00 0x01 is definitely Annex B (4-byte start code)
	// 0x00 0x00 0x01 XX could be Annex B 3-byte start code or AVCC with NAL size 256-511
	if (byteData[0] == 0x00 && byteData[1] == 0x00 && byteData[2] == 0x00 && byteData[3] == 0x01)
	{
		return false;
	}

	if (byteData[0] == 0x00 && byteData[1] == 0x00 && byteData[2] == 0x01)
	{
		const uint32_t possibleLength = (uint32_t(byteData[0]) << 24) | (uint32_t(byteData[1]) << 16) |
		                                (uint32_t(byteData[2]) << 8) | uint32_t(byteData[3]);

		if (possibleLength > 0 && possibleLength <= size - 4)
		{
			return true;
		}

		return false;
	}

	return true;
}

GUID VideoDecoder::mimeToVideoFormat(const std::string& mime)
{
	if (mime == "video/avc" || mime == "video/h264")
	{
		return MFVideoFormat_H264;
	}
	else if (mime == "video/hevc" || mime == "video/h265")
	{
		return MFVideoFormat_HEVC;
	}

	return GUID_NULL;
}

}

}

}
