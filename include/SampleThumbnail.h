/*
 * SampleThumbnail.h
 *
 * Copyright (c) 2024 Khoi Dau <casboi86@gmail.com>
 *
 * This file is part of LMMS - https://lmms.io
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program (see COPYING); if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301 USA.
 *
 */

#ifndef LMMS_SAMPLE_THUMBNAIL_H
#define LMMS_SAMPLE_THUMBNAIL_H

#include <QPainter>

#include "lmms_export.h"

namespace lmms {

class Sample;
class SampleFrame;

/**
   Insert this into your class when you want to implement
   thumbnails. This is optional, but without this class, there will be no
   indication that this thumbnail list is being used and it will be destroyed
   when you generate thumbnails somewhere else, and have to be regenerated
   before being rendered, which is slower than rendering the sample directly.
*/
class LMMS_EXPORT SampleThumbnail
{
public:
	static constexpr auto MinThumbnailSize = 1;
	static constexpr auto MaxThumbnailSize = 32768;
	static constexpr auto ThumbnailSizeDivisor = 32;

	struct Bit
	{
		Bit() = default;
		Bit(const SampleFrame&);

		void merge(const Bit&);
		void merge(const SampleFrame&);

		float max = -100.0f;
		float min = 100.0f;
		float rms = 0.0f;
	};

	struct VisualizeParameters
	{
		const Sample* originalSample; //!< Points to an original sample to render.

		float amplification = 1.0f; //!< The amount of amplification to apply to the waveform.
		bool reversed = false;		//!< Determines if the waveform is drawn in reverse or not.

		float sampleStart = 0.0f; //!< Where the sample begins for drawing.
		float sampleEnd = 1.0f;	  //!< Where the sample ends for drawing.

		long x = 0; //!< Starting X position for the waveform.
		long y = 0; //!< Starting Y position for the waveform.

		long width = 0;	 //!< The width of the rectangle to draw into.
		long height = 0; //!< The height of the rectangle to draw into.

		/**
			Song editor clips shorter than the sample length (measuring
			from the start of the sample) can specify this field so
			rendering cuts off early, reducing computation cost.
		*/
		long clipWidthSinceSampleStart = std::numeric_limits<long>::max();
	};

	using Thumbnail = std::vector<Bit>;
	using ThumbnailCache = std::vector<Thumbnail>;

	SampleThumbnail() = default;
	SampleThumbnail(const Sample& sample);

	void visualize(const VisualizeParameters& parameters, QPainter& painter) const;
	void visualizeOriginal(const VisualizeParameters& parameters, QPainter& painter) const;

	bool selectFromGlobalThumbnailMap(const Sample&);
	static void cleanUpGlobalThumbnailMap();

private:
	static void draw(QPainter& painter, const Bit& bit, float lineX, int centerY, float scalingFactor,
		const QColor& color, const QColor& rmsColor);

	static Thumbnail generate(const size_t thumbnailSize, const SampleFrame* buffer, const size_t size);

	std::shared_ptr<ThumbnailCache> m_thumbnailCache = nullptr;
	inline static std::map<const QString, std::shared_ptr<ThumbnailCache>> s_sampleThumbnailCacheMap;
};

} // namespace lmms

#endif // LMMS_SAMPLE_THUMBNAIL_H