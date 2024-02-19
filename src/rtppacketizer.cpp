/**
 * Copyright (c) 2020 Filip Klembara (in2core)
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#if RTC_ENABLE_MEDIA

#include "rtppacketizer.hpp"

#include <cmath>
#include <cstring>

namespace {

// TODO: move to utils
uint64_t ntp_time() {
	const auto now = std::chrono::system_clock::now();
	const double secs = std::chrono::duration<double>(now.time_since_epoch()).count();
	// Assume the epoch is 01/01/1970 and adds the number of seconds between 1900 and 1970
	return uint64_t(std::floor((secs + 2208988800.) * double(uint64_t(1) << 32)));
}

} // namespace

namespace rtc {

RtpPacketizer::RtpPacketizer(shared_ptr<RtpPacketizationConfig> rtpConfig) : rtpConfig(rtpConfig) {}

RtpPacketizer::~RtpPacketizer() {}

message_ptr RtpPacketizer::packetize(shared_ptr<binary> payload, bool mark) {
	size_t rtpExtHeaderSize = 0;

	const bool setVideoRotation = (rtpConfig->videoOrientationId != 0) &&
	                              (rtpConfig->videoOrientationId <
	                               15) && // needs fixing if longer extension headers are supported
	                              mark &&
	                              (rtpConfig->videoOrientation != 0);

	const bool setAbsSendTime = (rtpConfig->absSendTimeId != 0);
	
	if (setVideoRotation)
		rtpExtHeaderSize += 2;

	if (setAbsSendTime)
	    rtpExtHeaderSize += (1 + sizeof(uint32_t) - 1);

	if (rtpConfig->mid.has_value())
		rtpExtHeaderSize += (1 + rtpConfig->mid->length());

	if (rtpConfig->rid.has_value())
		rtpExtHeaderSize += (1 + rtpConfig->rid->length());

	if (rtpExtHeaderSize != 0)
		rtpExtHeaderSize += 4;

	rtpExtHeaderSize = (rtpExtHeaderSize + 3) & ~3;

	auto message = make_message(RtpHeaderSize + rtpExtHeaderSize + payload->size());
	auto *rtp = (RtpHeader *)message->data();
	rtp->setPayloadType(rtpConfig->payloadType);
	rtp->setSeqNumber(rtpConfig->sequenceNumber++); // increase sequence number
	rtp->setTimestamp(rtpConfig->timestamp);
	rtp->setSsrc(rtpConfig->ssrc);

	if (mark) {
		rtp->setMarker(true);
	}

	if (rtpExtHeaderSize) {
		rtp->setExtension(true);

		auto extHeader = rtp->getExtensionHeader();
		extHeader->setProfileSpecificId(0xbede);

		auto headerLength = static_cast<uint16_t>(rtpExtHeaderSize / 4) - 1;

		extHeader->setHeaderLength(headerLength);
		extHeader->clearBody();

		size_t offset = 0;
		if (setVideoRotation) {
			extHeader->writeCurrentVideoOrientation(offset, rtpConfig->videoOrientationId,
			                                        rtpConfig->videoOrientation);
			offset += 2;
		}

		// https://webrtc.googlesource.com/src/+/refs/heads/main/docs/native-code/rtp-hdrext/abs-send-time
		if (setAbsSendTime) {
			uint64_t ntpTime = ntp_time();
			uint32_t ntpTimestamp = ntpTime >> 14;
			std::byte nb[3];
			nb[0] = (std::byte)((ntpTimestamp & 0x00FF0000) >> 16);
			nb[1] = (std::byte)((ntpTimestamp & 0x0000FF00) >> 8);
			nb[2] = (std::byte)(ntpTimestamp & 0x000000FF);
			extHeader->writeOneByteHeader(offset, rtpConfig->absSendTimeId,
			    nb, sizeof(uint32_t) - 1);
			offset += (1 + sizeof(uint32_t) - 1);
		}

		if (rtpConfig->mid.has_value()) {
			extHeader->writeOneByteHeader(
			    offset, rtpConfig->midId,
			    reinterpret_cast<const std::byte *>(rtpConfig->mid->c_str()),
			    rtpConfig->mid->length());
			offset += (1 + rtpConfig->mid->length());
		}

		if (rtpConfig->rid.has_value()) {
			extHeader->writeOneByteHeader(
			    offset, rtpConfig->ridId,
			    reinterpret_cast<const std::byte *>(rtpConfig->rid->c_str()),
			    rtpConfig->rid->length());
		}
	}

	rtp->preparePacket();

	std::memcpy(message->data() + RtpHeaderSize + rtpExtHeaderSize, payload->data(),
	            payload->size());

	return message;
}

void RtpPacketizer::media([[maybe_unused]] const Description::Media &desc) {}

void RtpPacketizer::outgoing([[maybe_unused]] message_vector &messages,
                             [[maybe_unused]] const message_callback &send) {
	// Default implementation
	for (auto &message : messages)
		message = packetize(message, false);
}

} // namespace rtc

#endif /* RTC_ENABLE_MEDIA */
