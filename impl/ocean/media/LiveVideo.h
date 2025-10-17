/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#ifndef META_OCEAN_MEDIA_LIVE_VIDEO_H
#define META_OCEAN_MEDIA_LIVE_VIDEO_H

#include "ocean/media/Media.h"
#include "ocean/media/FrameMedium.h"
#include "ocean/media/LiveMedium.h"

namespace Ocean
{

namespace Media
{

// Forward declaration.
class LiveVideo;

/**
 * Definition of a smart medium reference holding a live video object.
 * @see SmartMediumRef, LiveVideo.
 * @ingroup media
 */
using LiveVideoRef = SmartMediumRef<LiveVideo>;

/**
 * This class is the base class for all live videos.
 * @ingroup media
 */
class OCEAN_MEDIA_EXPORT LiveVideo :
	public virtual FrameMedium,
	public virtual LiveMedium
{
	public:

		/**
		 * Definition of individual control modes.
		 * The modes are used for exposure, ISO, and focus.
		 */
		enum ControlMode : uint32_t
		{
			/// An invalid control mode.
			CM_INVALID = 0u,
			/// The control is fixed (e.g., because the exposure, ISO, or focus was set manually).
			CM_FIXED,
			/// The control is dynamic (e.g., because auto exposure, ISO, or focus is enabled).
			CM_DYNAMIC
		};

		/**
		 * Definition of a vector holding control modes.
		 */
		using ControlModes = std::vector<ControlMode>;

		/**
		 * Definition of individual stream types.
		 */
		enum StreamType : uint32_t
		{
			/// An invalid stream type.
			ST_INVALID = 0u,
			/// A stream composed of individual uncompressed frames with individual pixel formats (e.g., FORMAT_RGB24, FORMAT_YUYV16, etc.).
			ST_FRAME,
			/// A stream composed of Motion JPEG frames.
			ST_MJPEG,
			/// A stream composed of compressed frames with individual codecs (e.g., H264, H265).
			ST_CODEC
		};

		/**
		 * Definition of a vector holding stream types.
		 */
		using StreamTypes = std::vector<StreamType>;

		/**
		 * Definition of individual codec types.
		 */
		enum CodecType : uint32_t
		{
			/// An invalid codec type.
			CT_INVALID = 0u,
			/// Codec using H.264 for encoding or decoding.
			CT_H264,
			/// Codec using H.265 for encoding or decoding.
			CT_H265
		};

		/**
		 * This class holds the relevant information describing the property of a stream.
		 */
		class OCEAN_MEDIA_EXPORT StreamProperty
		{
			public:

				/**
				 * Simple helper struct allowing to calculate a hash value for stream property.
				 */
				struct Hash
				{
					/**
					 * Returns a hash value for a StreamProperty object.
					 * @param streamProperty The object for which the hash will be returned
					 * @return The hash value
					 */
					inline size_t operator()(const StreamProperty& streamProperty) const;
				};

			public:

				/**
				 * Default constructor creating an invalid object.
				 */
				StreamProperty() = default;

				/**
				 * Creates a new stream property object.
				 * @param streamType The type of the stream
				 * @param width The width of the stream in pixel
				 * @param height The height of the stream in pixel
				 * @param framePixelFormat The pixel format of the stream, invalid if stream type is not ST_FRAME
				 * @param codecType The type of the stream, invalid if stream type is not ST_CODEC
				 */
				StreamProperty(const StreamType streamType, const unsigned int width, unsigned int height, const FrameType::PixelFormat framePixelFormat, const CodecType codecType);

				/**
				 * Returns whether this configuration object holds a valid configuration.
				 * The configuration is valid if a valid stream type and a valid image resolution is defined.
				 * @return True, if so
				 */
				inline bool isValid() const;

				/**
				 * Returns whether this configuration object holds the same configuration as the given one.
				 * @param right The configuration object to compare
				 * @return True, if so
				 */
				bool operator==(const StreamProperty& right) const;

			public:

				/// The type of the stream.
				StreamType streamType_ = ST_INVALID;

				/// The width of the stream in pixel.
				unsigned int width_ = 0u;

				/// The height of the stream in pixel.
				unsigned int height_ = 0u;

				/// The pixel format of the stream, only valid if the stream type is ST_FRAME.
				FrameType::PixelFormat framePixelFormat_ = FrameType::FORMAT_UNDEFINED;

				/// The codec of the stream, only valid if the stream type is ST_CODEC.
				CodecType codecType_ = CT_INVALID;
		};

		/**
		 * This class holds the relevant information describing a video stream configuration.
		 */
		class OCEAN_MEDIA_EXPORT StreamConfiguration : public StreamProperty
		{
			public:

				/**
				 * Default constructor creating an invalid object.
				 */
				StreamConfiguration() = default;

				/**
				 * Creates a new stream configuration object.
				 * @param streamProperty The property of the stream
				 * @param frameRates The frame rates of the stream in Hz
				 */
				StreamConfiguration(const StreamProperty& streamProperty, std::vector<double>&& frameRates);

				/**
				 * Creates a new stream configuration object.
				 * @param streamType The type of the stream
				 * @param width The width of the stream in pixel
				 * @param height The height of the stream in pixel
				 * @param frameRates The frame rates of the stream in Hz
				 * @param framePixelFormat The pixel format of the stream, invalid if stream type is not ST_FRAME
				 * @param codecType The type of the stream, invalid if stream type is not ST_CODEC
				 */
				StreamConfiguration(const StreamType streamType, const unsigned int width, unsigned int height, std::vector<double>&& frameRates, const FrameType::PixelFormat framePixelFormat, const CodecType codecType);

				/**
				 * Returns a string containing the readable information of this stream configuration.
				 * @return The readable information of this stream configuration
				 */
				std::string toString() const;

			public:

				/// The frame rates of the stream in Hz.
				std::vector<double> frameRates_;
		};

		/**
		 * Definition of a vector holding stream configurations.
		 */
		using StreamConfigurations = std::vector<StreamConfiguration>;

	public:

		/**
		 * Returns the supported stream types.
		 * @return The types of all supported streams, empty if this object does not allow (or does not need) to select the stream type.
		 */
		virtual StreamTypes supportedStreamTypes() const;

		/**
		 * Returns the supported stream configurations for a given stream type.
		 * @param streamType The type of the stream for which the supported stream configurations will be returned, ST_INVALID to return all stream configurations for all stream types
		 * @return The supported stream configurations
		 */
		virtual StreamConfigurations supportedStreamConfigurations(const StreamType streamType = ST_INVALID) const;

		/**
		 * Returns the current exposure duration of this device.
		 * @param minDuration Optional resulting minimal duration to set, in seconds, with range (0, infinity), -1 if unknown
		 * @param maxDuration Optional resulting maximal duration to set, in seconds, with range [minDuration, infinity), -1 if unknown
		 * @param exposureMode Optional resulting exposure mode, nullptr if not of interest
		 * @return The duration in seconds, with range [minDuration, maxDuration], -1 if unknown
		 * @see setExposureDuration().
		 */
		virtual double exposureDuration(double* minDuration = nullptr, double* maxDuration = nullptr, ControlMode* exposureMode = nullptr) const;

		/**
		 * Returns the current ISO of this device.
		 * @param minISO Optional resulting minimal ISO to set, with range (0, infinity), -1 if unknown
		 * @param maxISO Optional resulting maximal ISO to set, with range (0, infinity), -1 if unknown
		 * @param isoMode Optional resulting ISO mode, nullptr if not of interest
		 * @return The current ISO, with range [minISO, maxISO], -1 if unknown
		 */
		virtual float iso(float* minISO = nullptr, float* maxISO = nullptr, ControlMode* isoMode = nullptr) const;

		/**
		 * Returns the current focus of this device.
		 * @return The device's focus, with range [0, 1] with 0 shortest distance and 1 furthest distance
		 * @param focusMode Optional resulting focus mode, nullptr if not of interest
		 */
		virtual float focus(ControlMode* focusMode = nullptr) const;

		/**
		 * Returns whether video stabilization is currently enabled.
		 * Video stabilization applies post-processing to reduce camera shake, which may introduce artificial warping or geometric distortions.
		 * @return True, if video stabilization is enabled; False, if video stabilization is disabled; depends on platform and device support
		 */
		virtual bool videoStabilization() const;

		/**
		 * Sets the preferred stream type.
		 * There is no guarantee that the device will use this stream type.
		 * @param streamType The preferred stream type to be set, must be valid
		 * @return True, if succeeded
		 * @see setPreferredStreamConfiguration().
		 */
		virtual bool setPreferredStreamType(const StreamType streamType);

		/**
		 * Sets the preferred stream configuration.
		 * Using this function will forward some settings to the underlying FrameMedium object via setPreferredFrameDimension(), setPreferredFramePixelFormat(), and setPreferredFrameFrequency().
		 * There is no guarantee that the device will use this stream type.
		 * @param streamConfiguration The preferred stream configuration to be set, must be valid
		 * @return True, if succeeded
		 * @see setPreferredStreamType(), FrameMedium::setPreferredFrameDimension(), FrameMedium::setPreferredFramePixelFormat(), and FrameMedium::setPreferredFrameFrequency()
		 */
		virtual bool setPreferredStreamConfiguration(const StreamConfiguration& streamConfiguration);

		/**
		 * Sets the exposure duration of this device.
		 * @param duration The exposure duration to be set, in seconds, with range (0, infinity), 0 for auto exposure, -1 for a one-time auto exposure
		 * @param allowShorterExposure True, to allow shorter exposure durations than the specified duration; False, to enforce the specified duration; Ignored if auto exposure is used
		 * @return True, if succeeded
		 * @see exposureDuration().
		 */
		virtual bool setExposureDuration(const double duration, const bool allowShorterExposure = false);

		/**
		 * Sets the ISO of this device.
		 * @param iso The ISO to be set, with range (0, infinity), -1 for auto ISO
		 * @return True, if succeeded
		 * @see iso().
		 */
		virtual bool setISO(const float iso);

		/**
		 * Sets the focus of this device.
		 * @param position The focus position to be set, with range [0, 1] with 0 shortest distance and 1 furthest distance, -1 for auto focus
		 * @return True, if succeeded
		 * @see focus().
		 */
		virtual bool setFocus(const float position);

		/**
		 * Sets whether video stabilization should be enabled.
		 * Video stabilization applies post-processing to reduce camera shake, but may introduce artificial warping or geometric distortions.
		 * Disabling video stabilization provides raw, unprocessed camera frames.
		 * @param enable True, to enable video stabilization; False, to disable video stabilization and get raw camera data
		 * @return True, if succeeded; False, if video stabilization control is not supported on this platform or device
		 * @see videoStabilization().
		 */
		virtual bool setVideoStabilization(const bool enable);

		/**
		 * Translates a control mode to a string.
		 * @param controlMode The control mode to translate
		 * @return The translated string, 'Invalid' if the control mode is invalid or unknown
		 */
		static std::string translateControlMode(const ControlMode controlMode);

		/**
		 * Translates a stream type to a string.
		 * @param streamType The stream type to be translated
		 * @return The translated string, 'Invalid' if the stream type is invalid or unknown
		 */
		static std::string translateStreamType(const StreamType streamType);

		/**
		 * Translates a codec type to a string.
		 * @param codecType The stream type to be translated
		 * @return The translated string, 'Invalid' if the codec type is invalid or unknown
		 */
		static std::string translateCodecType(const CodecType codecType);

	protected:

		/**
		 * Creates a new live video source by a given url.
		 * @param url Url of the live video source
		 */
		explicit LiveVideo(const std::string& url);
};

inline size_t LiveVideo::StreamProperty::Hash::operator()(const StreamProperty& streamProperty) const
{
	size_t seed = std::hash<StreamType>{}(streamProperty.streamType_);
	seed ^= std::hash<unsigned int>{}(streamProperty.width_) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
	seed ^= std::hash<unsigned int>{}(streamProperty.height_) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
	seed ^= std::hash<FrameType::PixelFormat>{}(streamProperty.framePixelFormat_) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
	seed ^= std::hash<CodecType>{}(streamProperty.codecType_) + 0x9e3779b9 + (seed << 6) + (seed >> 2);

	return seed;
}

inline bool LiveVideo::StreamProperty::isValid() const
{
	return streamType_ != ST_INVALID && width_ > 0u && height_ > 0u;
}

}

}

#endif // META_OCEAN_MEDIA_LIVE_VIDEO_H
