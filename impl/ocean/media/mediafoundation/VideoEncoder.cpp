/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "ocean/media/mediafoundation/VideoEncoder.h"

#include "ocean/cv/FrameConverter.h"

#include <codecapi.h>
#include <mferror.h>

namespace Ocean
{

namespace Media
{

namespace MediaFoundation
{

VideoEncoder::VideoEncoder()
{
	// nothing to do here
}

VideoEncoder::~VideoEncoder()
{
	release();
}

bool VideoEncoder::initialize(const unsigned int width, const unsigned int height, const std::string& mime, const double frameRate, const unsigned int bitrate, const int iFrameInterval)
{
	ocean_assert(width >= 1u && height >= 1u);
	ocean_assert(!mime.empty());
	ocean_assert(bitrate > 0u);
	ocean_assert(frameRate > 0.0);

	if (width == 0u || height == 0u || width > maximalWidth_ || height > maximalHeight_)
	{
		ocean_assert(false && "Invalid dimensions");
		return false;
	}

	if (bitrate == 0u || bitrate > (unsigned int)(maximalBitrate_))
	{
		ocean_assert(false && "Invalid bitrate");
		return false;
	}

	if (frameRate <= 0.0)
	{
		ocean_assert(false && "Invalid frame rate");
		return false;
	}

	if (mime.empty())
	{
		ocean_assert(false && "Invalid MIME type");
		return false;
	}

	const ScopedLock scopedLock(lock_);

	if (encoder_.isValid())
	{
		ocean_assert(false && "Already initialized");
		return false;
	}

	const HRESULT startupResult = MFStartup(MF_VERSION);

	if (S_OK != startupResult)
	{
		Log::error() << "VideoEncoder: Failed to initialize Media Foundation, error: 0x" << String::toAStringHex(int(startupResult));
		return false;
	}

	mfStarted_ = true;

	const GUID videoFormat = mimeToVideoFormat(mime);

	if (videoFormat == GUID_NULL)
	{
		Log::error() << "VideoEncoder: Unsupported MIME type: " << mime;
		return false;
	}

	MFT_REGISTER_TYPE_INFO outputTypeInfo = {MFMediaType_Video, videoFormat};

	IMFActivate** ppActivate = nullptr;
	UINT32 activateCount = 0;

	const UINT32 enumFlags = MFT_ENUM_FLAG_SYNCMFT | MFT_ENUM_FLAG_LOCALMFT | MFT_ENUM_FLAG_SORTANDFILTER;

	if (S_OK != MFTEnumEx(MFT_CATEGORY_VIDEO_ENCODER, enumFlags, nullptr, &outputTypeInfo, &ppActivate, &activateCount) || activateCount == 0u)
	{
		Log::error() << "VideoEncoder: No encoder found for MIME type: " << mime;

		if (ppActivate != nullptr)
		{
			CoTaskMemFree(ppActivate);
		}

		return false;
	}

	const UINT32 frameRateNumerator = UINT32(frameRate * 1000.0 + 0.5);
	const UINT32 frameRateDenominator = 1000u;

	bool encoderCreated = false;

	for (UINT32 i = 0u; i < activateCount; ++i)
	{
		ScopedIMFTransform encoder;

		if (S_OK != ppActivate[i]->ActivateObject(IID_PPV_ARGS(&encoder.resetObject())) || !encoder.isValid())
		{
			continue;
		}

		ScopedIMFMediaType outputType;

		if (S_OK != MFCreateMediaType(&outputType.resetObject()))
		{
			continue;
		}

		outputType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
		outputType->SetGUID(MF_MT_SUBTYPE, videoFormat);
		MFSetAttributeSize(*outputType, MF_MT_FRAME_SIZE, width, height);
		MFSetAttributeRatio(*outputType, MF_MT_FRAME_RATE, frameRateNumerator, frameRateDenominator);
		outputType->SetUINT32(MF_MT_AVG_BITRATE, bitrate);
		outputType->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);

		if (S_OK != encoder->SetOutputType(0, *outputType, 0))
		{
			continue;
		}

		bool inputTypeSet = false;

		for (DWORD typeIndex = 0u; ; ++typeIndex)
		{
			ScopedIMFMediaType availableInputType;

			if (S_OK != encoder->GetInputAvailableType(0, typeIndex, &availableInputType.resetObject()))
			{
				break;
			}

			GUID subtype = GUID_NULL;
			availableInputType->GetGUID(MF_MT_SUBTYPE, &subtype);

			if (subtype == MFVideoFormat_NV12)
			{
				MFSetAttributeSize(*availableInputType, MF_MT_FRAME_SIZE, width, height);
				MFSetAttributeRatio(*availableInputType, MF_MT_FRAME_RATE, frameRateNumerator, frameRateDenominator);
				availableInputType->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);

				if (S_OK == encoder->SetInputType(0, *availableInputType, 0))
				{
					inputTypeSet = true;
					break;
				}
			}
		}

		if (!inputTypeSet)
		{
			ScopedIMFMediaType inputType;

			if (S_OK == MFCreateMediaType(&inputType.resetObject()))
			{
				inputType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
				inputType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_NV12);
				MFSetAttributeSize(*inputType, MF_MT_FRAME_SIZE, width, height);
				MFSetAttributeRatio(*inputType, MF_MT_FRAME_RATE, frameRateNumerator, frameRateDenominator);
				inputType->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);

				// Set default stride (width aligned to 32 bytes for NV12)
				const UINT32 alignedStride = (width + 31u) & ~31u;
				inputType->SetUINT32(MF_MT_DEFAULT_STRIDE, alignedStride);

				inputTypeSet = (S_OK == encoder->SetInputType(0, *inputType, 0));
			}
		}

		if (!inputTypeSet)
		{
			continue;
		}

		encoder_ = std::move(encoder);
		encoderCreated = true;

		// Configure GOP size (keyframe interval) via ICodecAPI if supported
		ScopedICodecAPI codecApi;

		if (S_OK == encoder_->QueryInterface(IID_PPV_ARGS(&codecApi.resetObject())) && codecApi.isValid())
		{
			UINT32 gopSize = 0u;

			if (iFrameInterval < 0)
			{
				// Only first frame is a keyframe
				gopSize = UINT32_MAX;
			}
			else if (iFrameInterval == 0)
			{
				// Every frame is a keyframe
				gopSize = 1u;
			}
			else
			{
				// Keyframe every N seconds
				gopSize = UINT32(double(iFrameInterval) * frameRate + 0.5);

				if (gopSize == 0u)
				{
					gopSize = 1u;
				}
			}

			VARIANT gopValue;
			VariantInit(&gopValue);
			gopValue.vt = VT_UI4;
			gopValue.ulVal = gopSize;

			if (S_OK != codecApi->SetValue(&CODECAPI_AVEncMPVGOPSize, &gopValue))
			{
				Log::warning() << "VideoEncoder: Failed to set GOP size, encoder may use default keyframe interval";
			}
		}

		break;
	}

	for (UINT32 i = 0u; i < activateCount; ++i)
	{
		ppActivate[i]->Release();
	}

	CoTaskMemFree(ppActivate);

	if (!encoderCreated)
	{
		Log::error() << "VideoEncoder: Failed to create and configure encoder for MIME type: " << mime;
		return false;
	}

	MFT_OUTPUT_STREAM_INFO outputStreamInfo;

	if (S_OK == encoder_->GetOutputStreamInfo(0, &outputStreamInfo))
	{
		mftProvidesOutputSamples_ = (outputStreamInfo.dwFlags & (MFT_OUTPUT_STREAM_PROVIDES_SAMPLES | MFT_OUTPUT_STREAM_LAZY_READ)) != 0;
		outputBufferSize_ = outputStreamInfo.cbSize;
	}

	width_ = width;
	height_ = height;

	ocean_assert(isStarted_ == false);

	return true;
}

bool VideoEncoder::start()
{
	const ScopedLock scopedLock(lock_);

	if (!encoder_.isValid())
	{
		ocean_assert(false && "Not initialized");
		return false;
	}

	if (isStarted_)
	{
		return true;
	}

	// Send FLUSH before BEGIN_STREAMING to ensure the encoder is in a clean state
	encoder_->ProcessMessage(MFT_MESSAGE_COMMAND_FLUSH, 0);

	const HRESULT beginStreamingResult = encoder_->ProcessMessage(MFT_MESSAGE_NOTIFY_BEGIN_STREAMING, 0);

	if (S_OK != beginStreamingResult)
	{
		Log::error() << "VideoEncoder: Failed to begin streaming, error: 0x" << String::toAStringHex(int(beginStreamingResult));
		return false;
	}

	const HRESULT startStreamResult = encoder_->ProcessMessage(MFT_MESSAGE_NOTIFY_START_OF_STREAM, 0);

	if (S_OK != startStreamResult)
	{
		Log::error() << "VideoEncoder: Failed to start stream, error: 0x" << String::toAStringHex(int(startStreamResult));
		return false;
	}

	isStarted_ = true;

	return true;
}

bool VideoEncoder::stop()
{
	const ScopedLock scopedLock(lock_);

	if (!encoder_.isValid() || !isStarted_)
	{
		return true;
	}

	encoder_->ProcessMessage(MFT_MESSAGE_COMMAND_DRAIN, 0);
	drainOutputSamples();

	encoder_->ProcessMessage(MFT_MESSAGE_NOTIFY_END_OF_STREAM, 0);
	encoder_->ProcessMessage(MFT_MESSAGE_NOTIFY_END_STREAMING, 0);

	isStarted_ = false;

	return true;
}

bool VideoEncoder::pushFrame(const Frame& frame, const uint64_t presentationTime)
{
	ocean_assert(frame.isValid());

	if (!frame.isValid())
	{
		return false;
	}

	const ScopedLock scopedLock(lock_);

	if (!encoder_.isValid())
	{
		ocean_assert(false && "Not initialized");
		return false;
	}

	if (!isStarted_)
	{
		ocean_assert(false && "Not started");
		return false;
	}

	if (frame.width() != width_ || frame.height() != height_)
	{
		Log::error() << "VideoEncoder: Frame dimensions " << frame.width() << "x" << frame.height() << " don't match encoder dimensions " << width_ << "x" << height_;
		return false;
	}

	// Get the configured stride from the encoder's input type
	UINT32 expectedStride = width_;
	ScopedIMFMediaType currentInputType;

	if (S_OK == encoder_->GetInputCurrentType(0, &currentInputType.resetObject()))
	{
		UINT32 actualStride = 0;
		if (S_OK == currentInputType->GetUINT32(MF_MT_DEFAULT_STRIDE, &actualStride))
		{
			expectedStride = actualStride;
		}
		else
		{
			// If stride not set, use width (may be unaligned)
			expectedStride = width_;
		}
	}

	// NV12 format: Y plane followed by interleaved UV plane
	// Total size = stride * height * 3 / 2
	const DWORD yPlaneSize = expectedStride * height_;
	const DWORD uvPlaneSize = expectedStride * (height_ / 2u);
	const DWORD nv12BufferSize = yPlaneSize + uvPlaneSize;

	ScopedIMFMediaBuffer mediaBuffer;

	if (S_OK != MFCreateMemoryBuffer(nv12BufferSize, &mediaBuffer.resetObject()))
	{
		Log::error() << "VideoEncoder: Failed to create media buffer";
		return false;
	}

	BYTE* bufferData = nullptr;
	DWORD maxLength = 0;

	if (S_OK != mediaBuffer->Lock(&bufferData, &maxLength, nullptr) || bufferData == nullptr)
	{
		Log::error() << "VideoEncoder: Failed to lock media buffer";
		return false;
	}

	const LONG stride = LONG(expectedStride);

	const unsigned int absStride = (unsigned int)(stride < 0 ? -stride : stride);
	const FrameType targetFrameType(width_, height_, FrameType::FORMAT_Y_UV12_LIMITED_RANGE, FrameType::ORIGIN_UPPER_LEFT);

	unsigned int paddingElements = 0u;

	if (!Frame::strideBytes2paddingElements(FrameType::FORMAT_Y_UV12_LIMITED_RANGE, width_, absStride, paddingElements, 0u))
	{
		paddingElements = 0u;
	}

	const BYTE* yPlane = bufferData;
	BYTE* uvPlane = bufferData + size_t(absStride) * size_t(height_);

	Frame::PlaneInitializers<uint8_t> planeInitializers =
	{
		Frame::PlaneInitializer<uint8_t>((uint8_t*)yPlane, Frame::CM_USE_KEEP_LAYOUT, paddingElements),
		Frame::PlaneInitializer<uint8_t>((uint8_t*)uvPlane, Frame::CM_USE_KEEP_LAYOUT, paddingElements)
	};

	Frame targetFrame(targetFrameType, planeInitializers);

	if (!CV::FrameConverter::Comfort::convertAndCopy(frame, targetFrame))
	{
		Log::error() << "VideoEncoder: Failed to convert frame from " << FrameType::translatePixelFormat(frame.pixelFormat()) << " to NV12";
		mediaBuffer->Unlock();
		return false;
	}

	mediaBuffer->Unlock();
	mediaBuffer->SetCurrentLength(nv12BufferSize);

	ScopedIMFSample inputSample;

	if (S_OK != MFCreateSample(&inputSample.resetObject()))
	{
		Log::error() << "VideoEncoder: Failed to create sample";
		return false;
	}

	inputSample->AddBuffer(*mediaBuffer);
	inputSample->SetSampleTime(int64_t(presentationTime) * 10);

	// Set sample duration based on frame rate
	ScopedIMFMediaType inputMediaType;
	if (S_OK == encoder_->GetInputCurrentType(0, &inputMediaType.resetObject()))
	{
		UINT32 frameRateNum = 0, frameRateDenom = 0;
		if (S_OK == MFGetAttributeRatio(*inputMediaType, MF_MT_FRAME_RATE, &frameRateNum, &frameRateDenom) && frameRateNum > 0)
		{
			const LONGLONG sampleDuration = (LONGLONG)frameRateDenom * 10000000LL / (LONGLONG)frameRateNum;
			inputSample->SetSampleDuration(sampleDuration);
		}
	}

	HRESULT processInputResult = encoder_->ProcessInput(0, *inputSample, 0);

	if (processInputResult == MF_E_NOTACCEPTING)
	{
		drainOutputSamples();

		processInputResult = encoder_->ProcessInput(0, *inputSample, 0);

		if (S_OK != processInputResult)
		{
			Log::error() << "VideoEncoder: ProcessInput failed after drain, error: 0x" << String::toAStringHex(int(processInputResult));
			return false;
		}
	}
	else if (S_OK != processInputResult)
	{
		// Log detailed diagnostic info for invalid media type errors
		if (processInputResult == MF_E_INVALIDMEDIATYPE)
		{
			ScopedIMFMediaType diagInputType;
			if (S_OK == encoder_->GetInputCurrentType(0, &diagInputType.resetObject()))
			{
				UINT32 diagWidth = 0, diagHeight = 0;
				MFGetAttributeSize(*diagInputType, MF_MT_FRAME_SIZE, &diagWidth, &diagHeight);

				UINT32 diagStride = 0;
				diagInputType->GetUINT32(MF_MT_DEFAULT_STRIDE, &diagStride);

				GUID diagSubtype = GUID_NULL;
				diagInputType->GetGUID(MF_MT_SUBTYPE, &diagSubtype);

				Log::error() << "VideoEncoder: Encoder expects: " << diagWidth << "x" << diagHeight << ", stride: " << diagStride << ", buffer size: " << nv12BufferSize << ", stride used: " << expectedStride;
			}
		}

		Log::error() << "VideoEncoder: ProcessInput failed, error: 0x" << String::toAStringHex(int(processInputResult));
		return false;
	}

	drainOutputSamples();

	return true;
}

VideoEncoder::Sample VideoEncoder::popSample()
{
	const ScopedLock scopedLock(lock_);

	if (!encodedSamples_.empty())
	{
		Sample sample = std::move(encodedSamples_.front());
		encodedSamples_.pop_front();
		return sample;
	}

	if (!encoder_.isValid() || !isStarted_)
	{
		return Sample();
	}

	drainOutputSamples();

	if (!encodedSamples_.empty())
	{
		Sample sample = std::move(encodedSamples_.front());
		encodedSamples_.pop_front();
		return sample;
	}

	return Sample();
}

void VideoEncoder::release()
{
	const ScopedLock scopedLock(lock_);

	if (encoder_.isValid())
	{
		if (isStarted_)
		{
			stop();
		}

		encoder_.release();
	}

	encodedSamples_.clear();

	width_ = 0u;
	height_ = 0u;
	mftProvidesOutputSamples_ = false;
	outputBufferSize_ = 0u;
	codecConfigEmitted_ = false;

	if (mfStarted_)
	{
		MFShutdown();
		mfStarted_ = false;
	}
}

size_t VideoEncoder::drainOutputSamples()
{
	ocean_assert(encoder_.isValid());

	size_t samplesCollected = 0;

	while (true)
	{
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
				break;
			}

			DWORD bufferSize = outputBufferSize_;

			if (bufferSize == 0u)
			{
				bufferSize = width_ * height_; // rough estimate for encoded data
			}

			ScopedIMFMediaBuffer outputBuffer;

			if (S_OK != MFCreateMemoryBuffer(bufferSize, &outputBuffer.resetObject()))
			{
				break;
			}

			callerProvidedSample->AddBuffer(*outputBuffer);
			outputDataBuffer.pSample = *callerProvidedSample;
		}

		DWORD processOutputStatus = 0;
		const HRESULT processOutputResult = encoder_->ProcessOutput(0, 1, &outputDataBuffer, &processOutputStatus);

		if (processOutputResult == MF_E_TRANSFORM_NEED_MORE_INPUT)
		{
			if (outputDataBuffer.pEvents != nullptr)
			{
				outputDataBuffer.pEvents->Release();
			}

			break;
		}

		if (processOutputResult == MF_E_TRANSFORM_STREAM_CHANGE)
		{
			ScopedIMFMediaType outputType;

			if (S_OK == encoder_->GetOutputAvailableType(0, 0, &outputType.resetObject()))
			{
				encoder_->SetOutputType(0, *outputType, 0);
			}

			MFT_OUTPUT_STREAM_INFO streamInfo;

			if (S_OK == encoder_->GetOutputStreamInfo(0, &streamInfo))
			{
				mftProvidesOutputSamples_ = (streamInfo.dwFlags & (MFT_OUTPUT_STREAM_PROVIDES_SAMPLES | MFT_OUTPUT_STREAM_LAZY_READ)) != 0;
				outputBufferSize_ = streamInfo.cbSize;
			}

			if (outputDataBuffer.pEvents != nullptr)
			{
				outputDataBuffer.pEvents->Release();
			}

			continue;
		}

		if (S_OK != processOutputResult)
		{
			if (mftProvidesOutputSamples_ && outputDataBuffer.pSample != nullptr)
			{
				outputDataBuffer.pSample->Release();
			}

			if (outputDataBuffer.pEvents != nullptr)
			{
				outputDataBuffer.pEvents->Release();
			}

			break;
		}

		IMFSample* outputSample = outputDataBuffer.pSample;

		if (outputSample != nullptr)
		{
			LONGLONG sampleTime = 0;
			outputSample->GetSampleTime(&sampleTime);
			const int64_t samplePresentationTime = sampleTime / 10;

			UINT32 isCleanPoint = 0;
			const bool isKeyFrame = (S_OK == outputSample->GetUINT32(MFSampleExtension_CleanPoint, &isCleanPoint)) && isCleanPoint != 0;

			if (isKeyFrame && !codecConfigEmitted_)
			{
				ScopedIMFMediaType currentOutputType;

				if (S_OK == encoder_->GetOutputCurrentType(0, &currentOutputType.resetObject()))
				{
					UINT32 sequenceHeaderSize = 0;

					if (S_OK == currentOutputType->GetBlobSize(MF_MT_MPEG_SEQUENCE_HEADER, &sequenceHeaderSize) && sequenceHeaderSize > 0)
					{
						std::vector<uint8_t> configData(sequenceHeaderSize);

						if (S_OK == currentOutputType->GetBlob(MF_MT_MPEG_SEQUENCE_HEADER, configData.data(), sequenceHeaderSize, &sequenceHeaderSize))
						{
							configData.resize(sequenceHeaderSize);
							encodedSamples_.push_back(Sample(std::move(configData), samplePresentationTime, BUFFER_FLAG_CODEC_CONFIG));
							codecConfigEmitted_ = true;
						}
					}
				}
			}

			ScopedIMFMediaBuffer mediaBuffer;

			if (S_OK == outputSample->ConvertToContiguousBuffer(&mediaBuffer.resetObject()))
			{
				BYTE* bufferData = nullptr;
				DWORD bufferLength = 0;

				if (S_OK == mediaBuffer->Lock(&bufferData, nullptr, &bufferLength) && bufferData != nullptr && bufferLength > 0)
				{
					std::vector<uint8_t> encodedData(bufferLength);
					memcpy(encodedData.data(), bufferData, bufferLength);

					BufferFlags flags = BUFFER_FLAG_NONE;

					if (isKeyFrame)
					{
						flags = BufferFlags(flags | BUFFER_FLAG_KEY_FRAME);
					}

					encodedSamples_.push_back(Sample(std::move(encodedData), samplePresentationTime, flags));
					++samplesCollected;

					mediaBuffer->Unlock();
				}
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
	}

	return samplesCollected;
}

GUID VideoEncoder::mimeToVideoFormat(const std::string& mime)
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
