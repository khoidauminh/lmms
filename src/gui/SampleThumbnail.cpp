/*
 * SampleThumbnail.cpp
 *
 * Copyright (c) Copyright (c) 2024 Khoi Dau <casboi86@gmail.com>
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

#include "SampleThumbnail.h"
#include "Sample.h"

namespace lmms {

SampleThumbnail::Bit::Bit(const SampleFrame& frame)
	: max(std::max(frame.left(), frame.right()))
	, min(std::min(frame.left(), frame.right()))
	, rms(0.0)
{
}

void SampleThumbnail::Bit::merge(const Bit& other)
{
	min = std::min(min, other.min);
	max = std::max(max, other.max);
	rms = std::sqrt((rms * rms + other.rms * other.rms) / 2.0);
}

void SampleThumbnail::Bit::merge(const SampleFrame& frame)
{
	merge(Bit{frame});
}

/* DEPRECATED; functionality is kept for testing conveniences */
bool SampleThumbnail::selectFromGlobalThumbnailMap(const Sample& inputSample)
{
	const auto samplePtr = inputSample.buffer();
	const auto name = inputSample.sampleFile();
	const auto end = s_sampleThumbnailCacheMap.end();

	if (const auto list = s_sampleThumbnailCacheMap.find(name); list != end)
	{
		m_thumbnailCache = list->second;
		return true;
	}

	m_thumbnailCache = std::make_shared<SingleCache>();
	s_sampleThumbnailCacheMap.insert(std::make_pair(name, m_thumbnailCache));
	return false;
}

/* DEPRECATED; functionality is kept for testing conveniences */
void SampleThumbnail::cleanUpGlobalThumbnailMap()
{
	auto map = s_sampleThumbnailCacheMap.begin();
	while (map != s_sampleThumbnailCacheMap.end())
	{
		// All sample thumbnails are destroyed, a.k.a sample goes out of use
		if (map->second.use_count() == 1)
		{
			s_sampleThumbnailCacheMap.erase(map);
			map = s_sampleThumbnailCacheMap.begin();
			continue;
		}

		map++;
	}
}

SampleThumbnail::Thumbnail SampleThumbnail::generate(const std::size_t thumbnailSize, const SampleFrame* buffer, const std::size_t size)
{
	const auto sampleChunk = (size + thumbnailSize) / thumbnailSize;
	auto thumbnail = SampleThumbnail::Thumbnail(thumbnailSize);

	for (auto tIndex = std::size_t{0}; tIndex < thumbnailSize; tIndex++)
	{
		auto sampleIndex = tIndex * size / thumbnailSize;
		const auto sampleChunkBound = std::min(sampleIndex + sampleChunk, size);

		auto& bit = thumbnail[tIndex];
		while (sampleIndex < sampleChunkBound)
		{
			const auto& frame = buffer[sampleIndex];
			bit.merge(frame);

			const auto ave = frame.average();
			bit.rms += ave * ave;

			sampleIndex++;
		}

		bit.rms = std::sqrt(bit.rms / sampleChunk);
	}

	return thumbnail;
}

SampleThumbnail::SampleThumbnail(const Sample& inputSample)
{
	if (selectFromGlobalThumbnailMap(inputSample)) { return; }

	cleanUpGlobalThumbnailMap();

	const auto sampleBufferSize = inputSample.sampleSize();
	const auto& buffer = inputSample.data();

	const auto thumbnailSizeDivisor = std::max<size_t>(32, 3*std::log2(sampleBufferSize));
	// I don't think we *really* need to keep a full resolution thumbnail of the sample.
	const auto firstThumbnailSize = std::max<size_t>(sampleBufferSize / 4, 1);

	const auto firstThumbnail = generate(firstThumbnailSize, buffer, sampleBufferSize);
	m_thumbnailCache->thumbnails.push_back(firstThumbnail);

	// Generate the remaining thumbnails using the first one, each one's
	// size is the size of the previous one divided by the thumbnail size divisor.
	for (auto thumbnailSize = std::size_t{firstThumbnailSize / thumbnailSizeDivisor}; thumbnailSize >= MinThumbnailSize;
		 thumbnailSize /= thumbnailSizeDivisor)
	{
		const auto& biggerThumbnail = m_thumbnailCache->thumbnails.back();
		const auto biggerThumbnailSize = biggerThumbnail.size();
		auto bitIndex = std::size_t{0};

		auto thumbnail = Thumbnail(thumbnailSize);
		for (const auto& biggerBit : biggerThumbnail)
		{
			auto& bit = thumbnail[bitIndex * thumbnailSize / biggerThumbnailSize];

			bit.merge(biggerBit);

			++bitIndex;
		}

		m_thumbnailCache->thumbnails.push_back(thumbnail);
	}
	
	prerenderQPixmap();
}

void SampleThumbnail::draw(QPainter& painter, const SampleThumbnail::Bit& bit, float lineX, int centerY,
	float scalingFactor, const QColor& color, const QColor& rmsColor)
{
	const auto lengthY1 = bit.max * scalingFactor;
	const auto lengthY2 = bit.min * scalingFactor;

	const auto lineY1 = centerY - lengthY1;
	const auto lineY2 = centerY - lengthY2;

	const auto maxRMS = std::clamp(bit.rms, bit.min, bit.max);
	const auto minRMS = std::clamp(-bit.rms, bit.min, bit.max);

	const auto rmsLineY1 = centerY - maxRMS * scalingFactor;
	const auto rmsLineY2 = centerY - minRMS * scalingFactor;

	painter.drawLine(QPointF{lineX, lineY1}, QPointF{lineX, lineY2});
	painter.setPen(rmsColor);

	painter.drawLine(QPointF{lineX, rmsLineY1}, QPointF{lineX, rmsLineY2});
	painter.setPen(color);
}

void SampleThumbnail::drawPixmap(const SampleThumbnail::VisualizeParameters& parameters, QPainter& painter) const
{
	const auto& clipRect = parameters.clipRect;
	const auto& sampRect = parameters.sampRect.isNull() ? clipRect : parameters.sampRect;
	const auto& viewRect = parameters.viewRect.isNull() ? clipRect : parameters.viewRect;

	const auto sampleViewLength = parameters.sampleEnd - parameters.sampleStart;

	const auto x = sampRect.x();
	const auto height = clipRect.height();
	const auto halfHeight = height / 2;
	const auto width = sampRect.width();
	const auto centerY = clipRect.y() + halfHeight;

	if (width < 1)
	{
		return;
	}

	const auto scalingFactor = halfHeight * parameters.amplification;

	const auto color = painter.pen().color();
	const auto rmsColor = color.lighter(123);

	const auto widthSelect = static_cast<std::size_t>(static_cast<float>(width) / sampleViewLength);

	auto thumbnailIt = m_thumbnailCache->thumbnails.end();
	const auto thumbnailItStop =
		m_thumbnailCache->thumbnails.begin() + (parameters.allowHighResolution || m_thumbnailCache->thumbnails.size() == 1 ? 0 : 1);

	do {
		thumbnailIt--;
	} while (thumbnailIt != thumbnailItStop && thumbnailIt->size() < widthSelect);

	const auto& thumbnail = *thumbnailIt;

	const auto thumbnailSize = thumbnail.size();
	const auto thumbnailLastSample = std::max(static_cast<std::size_t>(parameters.sampleEnd * thumbnailSize), std::size_t{1}) - 1;
	const auto tStart = static_cast<long>(parameters.sampleStart * thumbnailSize);
	const auto thumbnailViewSize = thumbnailLastSample + 1 - tStart;
	const auto tLast = std::min<std::size_t>(thumbnailLastSample, thumbnailSize - 1);

	auto tIndex = std::size_t{0};
	const auto pixelIndexStart = std::max(x, std::max(clipRect.x(), viewRect.x()));
	const auto pixelIndexEnd = std::min(width, std::min(clipRect.width(), viewRect.width())) + pixelIndexStart;

	const auto tChunk = (thumbnailViewSize + width) / width;

	for (auto pixelIndex = pixelIndexStart; pixelIndex <= pixelIndexEnd; pixelIndex++)
	{
		tIndex = tStart + (pixelIndex - x) * thumbnailViewSize / width;

		if (tIndex > tLast) break;

		auto thumbnailBit = Bit{};

		const auto tChunkBound = tIndex + tChunk;

		while (tIndex < tChunkBound)
		{
			thumbnailBit.merge(thumbnail[parameters.reversed ? tLast - tIndex : tIndex]);
			tIndex += 1;
		}

		draw(painter, thumbnailBit, pixelIndex, centerY, scalingFactor, color, rmsColor);
	}
}

void SampleThumbnail::prerenderQPixmap()
{
	if (m_thumbnailCache == nullptr)
	{
		throw std::runtime_error("Nonexistent Thumbnail cache.");
	}

	for (const auto& width: SampleThumbnail::QPixmapWidths)
	{
		QPixmap pixmap = QPixmap(width, SampleThumbnail::QPixmapHeight);
		pixmap.fill(QColor(0, 0, 0, 0));
		QPainter p(&pixmap);
		p.setPen(QColor(192, 192, 192));

		VisualizeParameters param;
		param.allowHighResolution = true;
		param.amplification = 1.0;
		param.reversed = false;
		param.clipRect = QRect(0, 0, width, SampleThumbnail::QPixmapHeight);

		drawPixmap(param, p);
		p.end();

		m_thumbnailCache->qpixmaps.push_back(pixmap);
	}
}

void SampleThumbnail::visualize(const SampleThumbnail::VisualizeParameters& parameters, QPainter& painter) const
{
	const auto& clipRect = parameters.clipRect;
	const auto& sampRect = parameters.sampRect.isNull() ? clipRect : parameters.sampRect;
	const auto& viewRect = parameters.viewRect.isNull() ? clipRect : parameters.viewRect;

	const auto width = sampRect.width();
	const auto height = sampRect.height();

	if (width > SampleThumbnail::QPixmapWidthLimit)
	{
		drawPixmap(parameters, painter);
		return;
	}

	const auto pixmapIt = 
		std::find_if(
			m_thumbnailCache->qpixmaps.begin(), 
			m_thumbnailCache->qpixmaps.end(),
			[&](const auto& pixmap) { return pixmap.width() <= width; }
		);

	if (pixmapIt == m_thumbnailCache->qpixmaps.end())
	{
		drawPixmap(parameters, painter);
		return;
	}

	const auto& pixamp = *pixmapIt;
	const auto pixmapWidth = pixamp.width();
	const auto pixmapHeight = pixamp.height();

	const auto widthRatio = static_cast<float>(width) / pixmapWidth * (parameters.reversed ? -1.0 : 1.0);
	const auto heightRatio = static_cast<float>(height) / pixmapHeight;

	auto copyRect = sampRect;
	copyRect.setHeight(SampleThumbnail::QPixmapHeight);
	const auto toDraw = 
		pixamp
		.copy(copyRect)
		.transformed(QTransform().scale(widthRatio, heightRatio))
	;
	painter.drawPixmap(sampRect, toDraw, sampRect);
}

} // namespace lmms
