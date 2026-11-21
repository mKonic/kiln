#pragma once
// media3 test_data media/mpd samples, as DashManifestParserTest reads them.

#include <string_view>

namespace dash_test_data {

    constexpr std::string_view kLive = R"mpd(<?xml version="1.0" encoding="UTF-8"?>
<MPD xmlns:xsi="http://www.w3.org/2001/XMLSchema-instance"
    availabilityStartTime="2014-06-19T23:07:42"
    minBufferTime="PT1.500S"
    minimumUpdatePeriod="PT5.000S"
    profiles="urn:mpeg:dash:profile:isoff-main:2011"
    timeShiftBufferDepth="PT129600.000S"
    type="dynamic"
    xmlns="urn:mpeg:DASH:schema:MPD:2011"
    xsi:schemaLocation="urn:mpeg:DASH:schema:MPD:2011 DASH-MPD.xsd"
    yt:earliestMediaSequence="1266404" >
    <ProgramInformation lang="enUs"
        moreInformationURL="www.example.com">
        <Title>MediaTitle</Title>
        <Source>MediaSource</Source>
        <Copyright>MediaCopyright</Copyright>
    </ProgramInformation>
    <Period start="PT6462826.784S" >
        <SegmentList
            presentationTimeOffset="34740095"
            startNumber="1292317"
            timescale="1000" >
            <SegmentTimeline>
                <S d="4804" />
                <S d="5338" />
                <S d="4938" />
            </SegmentTimeline>
        </SegmentList>
        <AdaptationSet
            mimeType="audio/mp4"
            subsegmentAlignment="true" >
            <Role
                schemeIdUri="urn:mpeg:DASH:role:2011"
                value="main" />
            <Representation
                id="141"
                audioSamplingRate="48000"
                bandwidth="272000"
                codecs="mp4a.40.2"
                startWithSAP="1" >
                <AudioChannelConfiguration
                    schemeIdUri="urn:mpeg:dash:23003:3:audio_channel_configuration:2011"
                    value="2" />
                <BaseURL>
http://www.test.com/141
                </BaseURL>
                <SegmentList>
                    <Initialization
                        range="0-591"
                        sourceURL="sq/0/clen/79480/lmt/1403219262956762/dur/4.805" />
                    <SegmentURL media="sq/1292317/clen/77447/lmt/1409671169987621/dur/4.805" />
                    <SegmentURL media="sq/1292318/clen/86958/lmt/1409671174832549/dur/5.339" />
                    <SegmentURL media="sq/1292319/clen/85018/lmt/1409671179719956/dur/4.938" />
                </SegmentList>
            </Representation>
        </AdaptationSet>
        <AdaptationSet
            mimeType="video/mp4"
            subsegmentAlignment="true" >
            <Role
                schemeIdUri="urn:mpeg:DASH:role:2011"
                value="main" />
            <Representation
                id="135"
                bandwidth="1116000"
                codecs="avc1.42c01f"
                height="480"
                startWithSAP="1"
                width="854" >
                <BaseURL>
http://www.test.com/135
                </BaseURL>
                <SegmentList>
                    <Initialization
                        range="0-671"
                        sourceURL="sq/0/clen/1221137/lmt/1403219262956762/dur/4.805" />
                    <SegmentURL media="sq/1292317/clen/1279915/lmt/1409671169987621/dur/4.805" />
                    <SegmentURL media="sq/1292318/clen/1310650/lmt/1409671174832549/dur/5.339" />
                    <SegmentURL media="sq/1292319/clen/1486558/lmt/1409671179719956/dur/4.938" />
                </SegmentList>
            </Representation>
        </AdaptationSet>
        <AdaptationSet
            lang="en"
            mimeType="text/vtt" >
            <Role
                schemeIdUri="urn:mpeg:DASH:role:2011"
                value="caption" />
            <Representation
                id="en"
                bandwidth="0"
                codecs="" >
                <BaseURL>
http://www.test.com/vtt
                </BaseURL>
                <SegmentList>
                    <SegmentURL media="sq/1292317" />
                    <SegmentURL media="sq/1292318" />
                    <SegmentURL media="sq/1292319" />
                </SegmentList>
            </Representation>
        </AdaptationSet>
    </Period>
</MPD>
)mpd";
    constexpr std::string_view kLiveLocationRedirectRelative = R"mpd(<?xml version="1.0" encoding="utf-8"?>
<MPD
		type="dynamic"
		suggestedPresentationDelay="PT2S"
		availabilityStartTime="2020-01-01T00:00:00Z"
		minimumUpdatePeriod="PT4M"
		timeShiftBufferDepth="PT8.0S">
	<Location>../relative/redirect.mpd</Location>
	<Period start="PT0.0S">
		<AdaptationSet contentType="video">
			<Representation id="0" mimeType="video/mp4">
				<SegmentTemplate
						timescale="1000000"
						duration="2000000"
						availabilityTimeOffset="2"
						startNumber="1">
				</SegmentTemplate>
			</Representation>
		</AdaptationSet>
	</Period>
</MPD>
)mpd";
    constexpr std::string_view kLiveLocationRedirectAbsolute = R"mpd(<?xml version="1.0" encoding="utf-8"?>
<MPD
		type="dynamic"
		suggestedPresentationDelay="PT2S"
		availabilityStartTime="2020-01-01T00:00:00Z"
		minimumUpdatePeriod="PT4M"
		timeShiftBufferDepth="PT8.0S">
	<Location>https://example2.com/absolute/redirect.mpd</Location>
	<Period start="PT0.0S">
		<AdaptationSet contentType="video">
			<Representation id="0" mimeType="video/mp4">
				<SegmentTemplate
						timescale="1000000"
						duration="2000000"
						availabilityTimeOffset="2"
						startNumber="1">
				</SegmentTemplate>
			</Representation>
		</AdaptationSet>
	</Period>
</MPD>
)mpd";
    constexpr std::string_view kUnknownMimeType = R"mpd(<?xml version="1.0" encoding="UTF-8"?>
<MPD xmlns:xsi="http://www.w3.org/2001/XMLSchema-instance"
    availabilityStartTime="2014-06-19T23:07:42"
    minBufferTime="PT1.500S"
    minimumUpdatePeriod="PT5.000S"
    profiles="urn:mpeg:dash:profile:isoff-main:2011"
    timeShiftBufferDepth="PT129600.000S"
    type="dynamic"
    xmlns="urn:mpeg:DASH:schema:MPD:2011"
    xsi:schemaLocation="urn:mpeg:DASH:schema:MPD:2011 DASH-MPD.xsd"
    yt:earliestMediaSequence="1266404" >
    <Period start="PT6462826.784S" >
        <SegmentList
            presentationTimeOffset="34740095"
            startNumber="1292317"
            timescale="1000" >
            <SegmentTimeline>
                <S d="4804" />
                <S d="5338" />
                <S d="4938" />
            </SegmentTimeline>
        </SegmentList>
        <AdaptationSet
            mimeType="audio/mp4"
            subsegmentAlignment="true" >
            <Role
                schemeIdUri="urn:mpeg:DASH:role:2011"
                value="main" />
            <Representation
                id="141"
                audioSamplingRate="48000"
                bandwidth="272000"
                codecs="mp4a.40.2"
                startWithSAP="1" >
                <AudioChannelConfiguration
                    schemeIdUri="urn:mpeg:dash:23003:3:audio_channel_configuration:2011"
                    value="2" />
                <BaseURL>
http://www.test.com/141
                </BaseURL>
                <SegmentList>
                    <Initialization
                        range="0-591"
                        sourceURL="sq/0/clen/79480/lmt/1403219262956762/dur/4.805" />
                    <SegmentURL media="sq/1292317/clen/77447/lmt/1409671169987621/dur/4.805" />
                    <SegmentURL media="sq/1292318/clen/86958/lmt/1409671174832549/dur/5.339" />
                    <SegmentURL media="sq/1292319/clen/85018/lmt/1409671179719956/dur/4.938" />
                </SegmentList>
            </Representation>
        </AdaptationSet>
        <AdaptationSet
            mimeType="video/mp4"
            subsegmentAlignment="true" >
            <Role
                schemeIdUri="urn:mpeg:DASH:role:2011"
                value="main" />
            <Representation
                id="135"
                bandwidth="1116000"
                codecs="avc1.42c01f"
                height="480"
                startWithSAP="1"
                width="854" >
                <BaseURL>
http://www.test.com/135
                </BaseURL>
                <SegmentList>
                    <Initialization
                        range="0-671"
                        sourceURL="sq/0/clen/1221137/lmt/1403219262956762/dur/4.805" />
                    <SegmentURL media="sq/1292317/clen/1279915/lmt/1409671169987621/dur/4.805" />
                    <SegmentURL media="sq/1292318/clen/1310650/lmt/1409671174832549/dur/5.339" />
                    <SegmentURL media="sq/1292319/clen/1486558/lmt/1409671179719956/dur/4.938" />
                </SegmentList>
            </Representation>
        </AdaptationSet>
        <AdaptationSet
            lang="en"
            mimeType="text/vtt" >
            <Role
                schemeIdUri="urn:mpeg:DASH:role:2011"
                value="caption" />
            <Representation
                id="en"
                bandwidth="0"
                codecs="" >
                <BaseURL>
http://www.test.com/vtt
                </BaseURL>
                <SegmentList>
                    <SegmentURL media="sq/1292317" />
                    <SegmentURL media="sq/1292318" />
                    <SegmentURL media="sq/1292319" />
                </SegmentList>
            </Representation>
        </AdaptationSet>
        <AdaptationSet
                    mimeType="application/octet-stream" >
                    <Role
                        schemeIdUri="urn:mpeg:DASH:role:2011"
                        value="caption" />
                    <Representation
                        id="608"
                        bandwidth="0"
                        codecs="" >
                        <BaseURL>
        http://www.test.com/608
                        </BaseURL>
                        <SegmentList>
                            <SegmentURL media="sq/1292317" />
                            <SegmentURL media="sq/1292318" />
                            <SegmentURL media="sq/1292319" />
                        </SegmentList>
                    </Representation>
                </AdaptationSet>
    </Period>
</MPD>
)mpd";
    constexpr std::string_view kSegmentTemplate = R"mpd(<?xml version="1.0" encoding="UTF-8"?>
<MPD xmlns:xsi="http://www.w3.org/2001/XMLSchema-instance" xmlns="urn:mpeg:DASH:schema:MPD:2011" xmlns:yt="http://youtube.com/yt/2012/10/10" xsi:schemaLocation="urn:mpeg:DASH:schema:MPD:2011 DASH-MPD.xsd" minBufferTime="PT1.500S" profiles="urn:mpeg:dash:profile:isoff-main:2011" type="dynamic" availabilityStartTime="2016-10-14T17:00:17" timeShiftBufferDepth="PT7200.000S" minimumUpdatePeriod="PT2.000S" yt:earliestMediaSequence="0" yt:mpdRequestTime="2016-10-14T18:29:17.082" yt:mpdResponseTime="2016-10-14T18:29:17.194">
 <Period start="PT0.000S" yt:segmentIngestTime="2016-10-14T17:00:14.257">
  <SegmentTemplate startNumber="0" timescale="1000" media="sq/$Number$">
   <SegmentTimeline>
    <S d="2002" t="6009" r="2"/>
    <S d="1985"/>
    <S d="2000"/>
   </SegmentTimeline>
  </SegmentTemplate>
  <AdaptationSet id="0" mimeType="audio/mp4" subsegmentAlignment="true">
   <Role schemeIdUri="urn:mpeg:DASH:role:2011" value="main"/>
   <Representation id="140" codecs="mp4a.40.2" audioSamplingRate="48000" startWithSAP="1" bandwidth="144000">
    <AudioChannelConfiguration schemeIdUri="urn:mpeg:dash:23003:3:audio_channel_configuration:2011" value="2"/>
    <BaseURL>http://redirector.googlevideo.com/videoplayback/id/BktsoMO3OMs.0/itag/140/source/yt_live_broadcast/ratebypass/yes/cmbypass/yes/mime/audio%2Fmp4/live/1/gir/yes/noclen/1/signature/B5137EA0CC278C07DD056D204E863CC81EDEB39E.1AD5D242EBC94922EDA7165353A89A5E08A4103A/key/dg_test0/mpd_version/5/ip/0.0.0.0/ipbits/0/expire/1476490914/sparams/ip,ipbits,expire,id,itag,source,ratebypass,cmbypass,mime,live,gir,noclen/</BaseURL>
   </Representation>
  </AdaptationSet>
  <AdaptationSet id="1" mimeType="video/mp4" subsegmentAlignment="true">
   <Role schemeIdUri="urn:mpeg:DASH:role:2011" value="main"/>
   <Representation id="133" codecs="avc1.4d4015" width="426" height="240" startWithSAP="1" maxPlayoutRate="1" bandwidth="258000" frameRate="30">
    <BaseURL>http://redirector.googlevideo.com/videoplayback/id/BktsoMO3OMs.0/itag/133/source/yt_live_broadcast/ratebypass/yes/cmbypass/yes/mime/video%2Fmp4/live/1/gir/yes/noclen/1/signature/90154AE9C5C9D9D519CBF2E43AB0A1778375992D.40E2E855ADFB38FA7E95E168FEEEA6796B080BD7/key/dg_test0/mpd_version/5/ip/0.0.0.0/ipbits/0/expire/1476490914/sparams/ip,ipbits,expire,id,itag,source,ratebypass,cmbypass,mime,live,gir,noclen/</BaseURL>
   </Representation>
   <Representation id="134" codecs="avc1.4d401e" width="640" height="360" startWithSAP="1" maxPlayoutRate="1" bandwidth="646000" frameRate="30">
    <BaseURL>http://redirector.googlevideo.com/videoplayback/id/BktsoMO3OMs.0/itag/134/source/yt_live_broadcast/ratebypass/yes/cmbypass/yes/mime/video%2Fmp4/live/1/gir/yes/noclen/1/signature/5C094AEFDCEB1A4D2F3C05F8BD095C336EF0E1C3.7AE6B9951B0237AAE6F031927AACAC4974BAFFAA/key/dg_test0/mpd_version/5/ip/0.0.0.0/ipbits/0/expire/1476490914/sparams/ip,ipbits,expire,id,itag,source,ratebypass,cmbypass,mime,live,gir,noclen/</BaseURL>
   </Representation>
   <Representation id="135" codecs="avc1.4d401f" width="854" height="480" startWithSAP="1" maxPlayoutRate="1" bandwidth="1171000" frameRate="30">
    <BaseURL>http://redirector.googlevideo.com/videoplayback/id/BktsoMO3OMs.0/itag/135/source/yt_live_broadcast/ratebypass/yes/cmbypass/yes/mime/video%2Fmp4/live/1/gir/yes/noclen/1/signature/1F7660CA4E5B4AE4D60E18795680E34CDD2EF3C9.800B0A1D5F490DE142CCF4C88C64FD21D42129/key/dg_test0/mpd_version/5/ip/0.0.0.0/ipbits/0/expire/1476490914/sparams/ip,ipbits,expire,id,itag,source,ratebypass,cmbypass,mime,live,gir,noclen/</BaseURL>
   </Representation>
   <Representation id="160" codecs="avc1.42c00b" width="256" height="144" startWithSAP="1" maxPlayoutRate="1" bandwidth="124000" frameRate="30">
    <BaseURL>http://redirector.googlevideo.com/videoplayback/id/BktsoMO3OMs.0/itag/160/source/yt_live_broadcast/ratebypass/yes/cmbypass/yes/mime/video%2Fmp4/live/1/gir/yes/noclen/1/signature/94EB61673784DF0C4237A1A866F2E171C8A64ADB.AEC00AA06C2278FEA8702FB62693B70D8977F46C/key/dg_test0/mpd_version/5/ip/0.0.0.0/ipbits/0/expire/1476490914/sparams/ip,ipbits,expire,id,itag,source,ratebypass,cmbypass,mime,live,gir,noclen/</BaseURL>
   </Representation>
   <Representation id="136" codecs="avc1.4d401f" width="1280" height="720" startWithSAP="1" maxPlayoutRate="1" bandwidth="2326000" frameRate="30">
    <BaseURL>http://redirector.googlevideo.com/videoplayback/id/BktsoMO3OMs.0/itag/136/source/yt_live_broadcast/ratebypass/yes/cmbypass/yes/mime/video%2Fmp4/live/1/gir/yes/noclen/1/signature/6D8C34FC30A1F1A4F700B61180D1C4CCF6274844.29EBCB4A837DE626C52C66CF650519E61C2FF0BF/key/dg_test0/mpd_version/5/ip/0.0.0.0/ipbits/0/expire/1476490914/sparams/ip,ipbits,expire,id,itag,source,ratebypass,cmbypass,mime,live,gir,noclen/</BaseURL>
   </Representation>
  </AdaptationSet>
 </Period>
</MPD>
)mpd";
    constexpr std::string_view kEventStream = R"mpd(<?xml version="1.0" encoding="UTF-8"?>
<MPD xmlns:xsi="http://www.w3.org/2001/XMLSchema-instance" xmlns="urn:mpeg:DASH:schema:MPD:2011" xmlns:yt="http://youtube.com/yt/2012/10/10" xsi:schemaLocation="urn:mpeg:DASH:schema:MPD:2011 DASH-MPD.xsd" minBufferTime="PT1.500S" profiles="urn:mpeg:dash:profile:isoff-main:2011" type="dynamic" availabilityStartTime="2016-10-14T17:00:17" timeShiftBufferDepth="PT7200.000S" minimumUpdatePeriod="PT2.000S" yt:earliestMediaSequence="0" yt:mpdRequestTime="2016-10-14T18:29:17.082" yt:mpdResponseTime="2016-10-14T18:29:17.194">
 <Period start="PT0.000S" yt:segmentIngestTime="2016-10-14T17:00:14.257">
   <EventStream schemeIdUri="urn:uuid:XYZY" timescale="1000" value="call">
     <Event presentationTime="0" duration="10000" id="0" messageData="+ 1 800 10101010"/>
   </EventStream>
   <EventStream schemeIdUri="urn:uuid:with-pto" timescale="1000" presentationTimeOffset="4000" value="pto-4s">
     <Event presentationTime="5000" duration="10000" id="0" messageData="pt=1s"/>
   </EventStream>
   <EventStream schemeIdUri="urn:dvb:iptv:cpm:2014">
     <Event presentationTime="300" duration="1500" id="1"><![CDATA[<BroadcastEvent>
      <Program crid="crid://broadcaster.example.com/ABCDEF"/>
      <InstanceDescription>
      <Title xml:lang="en">The title</Title>
      <Synopsis xml:lang="en" length="medium">The description</Synopsis>
      <ParentalGuidance>
      <mpeg7:ParentalRating href="urn:dvb:iptv:rating:2014:15"/>
      <mpeg7:Region>GB</mpeg7:Region>
      </ParentalGuidance>
      </InstanceDescription>
      </BroadcastEvent>]]></Event>
   </EventStream>
   <EventStream schemeIdUri="urn:scte:scte35:2014:xml+bin">
     <Event timescale="90000" presentationTime="1000" duration="1000" id="2"><scte35:Signal>
         <scte35:Binary>
         /DAIAAAAAAAAAAAQAAZ/I0VniQAQAgBDVUVJQAAAAH+cAAAAAA==
         </scte35:Binary>
       </scte35:Signal></Event>
   </EventStream>
  <SegmentTemplate startNumber="0" timescale="1000" media="sq/$Number$">
   <SegmentTimeline>
    <S d="2002" t="6009" r="2"/>
   </SegmentTimeline>
  </SegmentTemplate>
  <AdaptationSet id="0" mimeType="audio/mp4" subsegmentAlignment="true">
   <Role schemeIdUri="urn:mpeg:DASH:role:2011" value="main"/>
   <Representation id="140" codecs="mp4a.40.2" audioSamplingRate="48000" startWithSAP="1" bandwidth="144000">
    <AudioChannelConfiguration schemeIdUri="urn:mpeg:dash:23003:3:audio_channel_configuration:2011" value="2"/>
    <BaseURL>http://www.test.url/</BaseURL>
   </Representation>
  </AdaptationSet>
  <AdaptationSet id="1" mimeType="video/mp4" subsegmentAlignment="true">
   <Role schemeIdUri="urn:mpeg:DASH:role:2011" value="main"/>
   <Representation id="133" codecs="avc1.4d4015" width="426" height="240" startWithSAP="1" maxPlayoutRate="1" bandwidth="258000" frameRate="30">
    <BaseURL>http://www.test.url/</BaseURL>
   </Representation>
  </AdaptationSet>
 </Period>
</MPD>
)mpd";
    constexpr std::string_view kImages = R"mpd(<?xml version="1.0" encoding="UTF-8"?>
<MPD type="static" duration="1s" mediaPresentationDuration="PT1000S">
 <Period>
  <AdaptationSet id="3" mimeType="image/jpeg" contentType="image">
    <SegmentTemplate media="$RepresentationID$/tile_$Number$.jpg" duration="100" startNumber="1"/>
    <Representation bandwidth="1234" id="images_320x180" width="320" height="180">
      <EssentialProperty schemeIdUri="http://dashif.org/thumbnail_tile" value="12x16"/>
    </Representation>
    <Representation bandwidth="2345" id="images_640x360" width="640" height="360">
      <EssentialProperty schemeIdUri="http://dashif.org/guidelines/thumbnail_tile" value="2x4"/>
    </Representation>
  </AdaptationSet>
 </Period>
</MPD>
)mpd";
    constexpr std::string_view kLabels = R"mpd(<?xml version="1.0" encoding="UTF-8"?>
<MPD type="static" duration="1s" mediaPresentationDuration="PT1S">
 <Period>
  <SegmentTemplate startNumber="0" timescale="1000" media="sq/$Number$">
   <SegmentTimeline>
    <S d="1000"/>
   </SegmentTimeline>
  </SegmentTemplate>
  <AdaptationSet id="0" mimeType="audio/mp4" subsegmentAlignment="true" label="audio label">
   <Representation id="0" codecs="mp4a.40.2" audioSamplingRate="48000" bandwidth="144000">
    <BaseURL>https://test.com/0</BaseURL>
   </Representation>
  </AdaptationSet>
  <AdaptationSet id="1" mimeType="video/mp4" subsegmentAlignment="true" label="ignored label">
   <Representation id="1" codecs="avc1.4d4015" width="426" height="240" bandwidth="258000">
    <BaseURL>https://test.com/1</BaseURL>
   </Representation>
   <Label>video label</Label>
  </AdaptationSet>
 </Period>
</MPD>
)mpd";
    constexpr std::string_view kAssetIdentifier = R"mpd(<?xml version="1.0" encoding="UTF-8"?>
<MPD xmlns:xsi="http://www.w3.org/2001/XMLSchema-instance" xmlns="urn:mpeg:DASH:schema:MPD:2011" xmlns:yt="http://youtube.com/yt/2012/10/10" xsi:schemaLocation="urn:mpeg:DASH:schema:MPD:2011 DASH-MPD.xsd" minBufferTime="PT1.500S" profiles="urn:mpeg:dash:profile:isoff-main:2011" type="dynamic" availabilityStartTime="2016-10-14T17:00:17" timeShiftBufferDepth="PT7200.000S" minimumUpdatePeriod="PT2.000S" yt:earliestMediaSequence="0" yt:mpdRequestTime="2016-10-14T18:29:17.082" yt:mpdResponseTime="2016-10-14T18:29:17.194">
 <Period start="PT0.000S" yt:segmentIngestTime="2016-10-14T17:00:14.257">
   <AssetIdentifier schemeIdUri="urn:org:dashif:asset-id:2013" value="md:cid:EIDR:10.5240%2f0EFB-02CD-126E-8092-1E49-W" id="uniqueId"/>
  <SegmentTemplate startNumber="0" timescale="1000" media="sq/$Number$">
   <SegmentTimeline>
    <S d="2002" t="6009" r="2"/>
   </SegmentTimeline>
  </SegmentTemplate>
  <AdaptationSet id="0" mimeType="audio/mp4" subsegmentAlignment="true">
   <Role schemeIdUri="urn:mpeg:DASH:role:2011" value="main"/>
   <Representation id="140" codecs="mp4a.40.2" audioSamplingRate="48000" startWithSAP="1" bandwidth="144000">
    <AudioChannelConfiguration schemeIdUri="urn:mpeg:dash:23003:3:audio_channel_configuration:2011" value="2"/>
    <BaseURL>http://www.test.url/</BaseURL>
   </Representation>
  </AdaptationSet>
  <AdaptationSet id="1" mimeType="video/mp4" subsegmentAlignment="true">
   <Role schemeIdUri="urn:mpeg:DASH:role:2011" value="main"/>
   <Representation id="133" codecs="avc1.4d4015" width="426" height="240" startWithSAP="1" maxPlayoutRate="1" bandwidth="258000" frameRate="30">
    <BaseURL>http://www.test.url/</BaseURL>
   </Representation>
  </AdaptationSet>
 </Period>
</MPD>
)mpd";
    constexpr std::string_view kText = R"mpd(<?xml version="1.0" encoding="UTF-8"?>
<MPD type="static" duration="1s" mediaPresentationDuration="PT1S">
 <Period>
  <SegmentTemplate startNumber="0" timescale="1000" media="sq/$Number$">
   <SegmentTimeline>
    <S d="1000"/>
   </SegmentTimeline>
  </SegmentTemplate>
  <AdaptationSet id="0" mimeType="application/mp4" subsegmentAlignment="true">
   <Role schemeIdUri="urn:mpeg:DASH:role:2011" value="forced-subtitle"/>
   <Representation id="0" codecs="stpp.ttml.im1t" bandwidth="16">
    <BaseURL>https://test.com/0</BaseURL>
   </Representation>
  </AdaptationSet>
  <AdaptationSet id="0" mimeType="application/mp4" subsegmentAlignment="true">
     <Role schemeIdUri="urn:mpeg:DASH:role:2011" value="forced_subtitle"/>
     <Representation id="0" codecs="stpp.ttml.im1t" bandwidth="16">
      <BaseURL>https://test.com/0</BaseURL>
     </Representation>
    </AdaptationSet>
  <AdaptationSet id="0" mimeType="application/ttml+xml" subsegmentAlignment="true">
   <Representation id="0" bandwidth="16">
    <BaseURL>https://test.com/0</BaseURL>
   </Representation>
  </AdaptationSet>
 </Period>
</MPD>
)mpd";
    constexpr std::string_view kTextPresentationTimeOffset = R"mpd(<?xml version="1.0" encoding="UTF-8"?>
<MPD type="static" mediaPresentationDuration="PT10S">
 <Period>
  <!-- Standalone TTML with @presentationTimeOffset. Per DASH-IF Standalone Text Timing
       (https://dashif.org/Guidelines-TimingModel/#standalone-text-timing), the offset SHALL be
       ignored by clients. -->
  <AdaptationSet id="0" mimeType="application/ttml+xml" subsegmentAlignment="true">
   <SegmentTemplate timescale="1000" presentationTimeOffset="2000" startNumber="0" media="ttml/$Number$">
    <SegmentTimeline>
     <S t="2000" d="10000"/>
    </SegmentTimeline>
   </SegmentTemplate>
   <Representation id="0" bandwidth="16">
    <BaseURL>https://test.com/0</BaseURL>
   </Representation>
  </AdaptationSet>
  <!-- Standalone WebVTT with @presentationTimeOffset. Must also be ignored. -->
  <AdaptationSet id="1" mimeType="text/vtt" subsegmentAlignment="true">
   <SegmentTemplate timescale="1000" presentationTimeOffset="3000" startNumber="0" media="vtt/$Number$">
    <SegmentTimeline>
     <S t="3000" d="10000"/>
    </SegmentTimeline>
   </SegmentTemplate>
   <Representation id="1" bandwidth="16">
    <BaseURL>https://test.com/1</BaseURL>
   </Representation>
  </AdaptationSet>
  <!-- TTML muxed inside mp4. This is NOT standalone text and @presentationTimeOffset must be
       honored. -->
  <AdaptationSet id="2" mimeType="application/mp4" subsegmentAlignment="true">
   <SegmentTemplate timescale="1000" presentationTimeOffset="5000" startNumber="0" media="mp4/$Number$">
    <SegmentTimeline>
     <S t="5000" d="10000"/>
    </SegmentTimeline>
   </SegmentTemplate>
   <Representation id="2" codecs="stpp.ttml.im1t" bandwidth="16">
    <BaseURL>https://test.com/2</BaseURL>
   </Representation>
  </AdaptationSet>
 </Period>
</MPD>
)mpd";
    constexpr std::string_view kTrickPlay = R"mpd(<?xml version="1.0" encoding="UTF-8"?>
<MPD type="static" duration="1s" mediaPresentationDuration="PT1S">
 <Period>
  <SegmentTemplate startNumber="0" timescale="1000" media="sq/$Number$">
   <SegmentTimeline>
    <S d="1000"/>
   </SegmentTimeline>
  </SegmentTemplate>
  <AdaptationSet id="0" mimeType="video/mp4" subsegmentAlignment="true">
   <Representation id="0" codecs="avc1.42c01f" bandwidth="128000">
    <BaseURL>https://test.com/0</BaseURL>
   </Representation>
  </AdaptationSet>
  <AdaptationSet id="1" mimeType="video/mp4" subsegmentAlignment="true">
   <Representation id="0" codecs="avc1.42c01f" bandwidth="128000">
    <BaseURL>https://test.com/0</BaseURL>
   </Representation>
  </AdaptationSet>
  <AdaptationSet id="2" mimeType="video/mp4" subsegmentAlignment="true">
   <EssentialProperty schemeIdUri="http://dashif.org/guidelines/trickmode" value="0"/>
   <Representation id="0" codecs="avc1.42c01f" bandwidth="128000">
    <BaseURL>https://test.com/0</BaseURL>
   </Representation>
  </AdaptationSet>
  <AdaptationSet id="3" mimeType="video/mp4" subsegmentAlignment="true">
   <SupplementalProperty schemeIdUri="http://dashif.org/guidelines/trickmode" value="1"/>
   <Representation id="0" codecs="avc1.42c01f" bandwidth="128000">
    <BaseURL>https://test.com/0</BaseURL>
   </Representation>
  </AdaptationSet>
 </Period>
</MPD>
)mpd";
    constexpr std::string_view kEssentialSupplementalProperties = R"mpd(<?xml version="1.0" encoding="UTF-8"?>
<MPD availabilityStartTime="2014-06-19T23:07:42" type="dynamic">
    <Period start="PT7462826.784S" id="0">
        <AdaptationSet id="0" mimeType="video/mp4">
            <SupplementalProperty
                schemeIdUri="urn:mpeg:dash:supplemental-scheme:2050"
                value="adaptationSupplemental"/>
            <EssentialProperty
                schemeIdUri="urn:mpeg:dash:essential-scheme:2050"
                value="adaptationEssential"/>
            <Representation id="singleSegment">
               <SegmentBase/>
               <SupplementalProperty
                   schemeIdUri="urn:mpeg:dash:supplemental-scheme:2050"
                   value="representationSupplemental"/>
               <EssentialProperty
                   schemeIdUri="urn:mpeg:dash:essential-scheme:2050"
                   value="representationEssential"/>
            </Representation>
            <Representation id="multiSegment">
               <SegmentList/>
               <SupplementalProperty
                   schemeIdUri="urn:mpeg:dash:supplemental-scheme:2050"
                   value="representationSupplemental"/>
               <EssentialProperty
                   schemeIdUri="urn:mpeg:dash:essential-scheme:2050"
                   value="representationEssential"/>
            </Representation>
        </AdaptationSet>
    </Period>
</MPD>
)mpd";
    constexpr std::string_view kAvailabilityTimeOffsetBaseUrl = R"mpd(<?xml version="1.0" encoding="UTF-8"?>
<MPD xmlns:xsi="http://www.w3.org/2001/XMLSchema-instance"
  xmlns="urn:mpeg:DASH:schema:MPD:2011"
  xsi:schemaLocation="urn:mpeg:DASH:schema:MPD:2011 DASH-MPD.xsd"
  xmlns:dvb="urn:dvb:dash:dash-extensions:2014-1"
  profiles="urn:mpeg:dash:profile:isoff-main:2011,urn:dvb:dash:profile:dvb-dash:2014"
  type="dynamic"
  availabilityStartTime="2016-10-14T17:00:17">
  <BaseURL>http://video.com/baseUrl</BaseURL>
  <Period start="PT0.000S">
    <BaseURL
      dvb:priority="2"
      dvb:weight="20"
      serviceLocation="period0"
      availabilityTimeOffset="5">http://video.com/baseUrl/period</BaseURL>
    <SegmentTemplate/>
    <AdaptationSet>
      <Representation/>
    </AdaptationSet>
    <AdaptationSet>
      <BaseURL
        dvb:priority="3"
        dvb:weight="30"
        serviceLocation="adaptationSet1"
        availabilityTimeOffset="4.321">http://video.com/baseUrl/adaptationSet1</BaseURL>
      <SegmentTemplate/>
      <Representation/>
    </AdaptationSet>
    <AdaptationSet>
      <SegmentTemplate/>
      <Representation>
        <BaseURL
          dvb:priority="4"
          dvb:weight="40"
          serviceLocation="representation2"
          availabilityTimeOffset="9.876543210">http://video.com/baseUrl/representation2</BaseURL>
        <SegmentTemplate/>
      </Representation>
    </AdaptationSet>
    <AdaptationSet>
      <BaseURL availabilityTimeOffset="0.5">http://video-foo.com/baseUrl/adaptationSet3</BaseURL>
      <Representation>
        <BaseURL
          dvb:priority="5"
          dvb:weight="50"
          serviceLocation="representation3"
          availabilityTimeOffset="INF">/baseUrl/representation3</BaseURL>
        <SegmentTemplate/>
      </Representation>
    </AdaptationSet>
  </Period>
  <Period start="PT100.000S">
    <SegmentTemplate/>
    <AdaptationSet>
      <Representation/>
    </AdaptationSet>
  </Period>
</MPD>
)mpd";
    constexpr std::string_view kMultipleBaseUrls = R"mpd(<?xml version="1.0" encoding="UTF-8"?>
<MPD xmlns:xsi="http://www.w3.org/2001/XMLSchema-instance"
  xmlns="urn:mpeg:DASH:schema:MPD:2011"
  xsi:schemaLocation="urn:mpeg:DASH:schema:MPD:2011 DASH-MPD.xsd"
  xmlns:dvb="urn:dvb:dash:dash-extensions:2014-1"
  profiles="urn:mpeg:dash:profile:isoff-main:2011,urn:dvb:dash:profile:dvb-dash:2014"
  type="dynamic"
  availabilityStartTime="2016-10-14T17:00:17">
  <BaseURL serviceLocation="a" dvb:priority="1" dvb:weight="1">http://video.com/baseUrl/a/</BaseURL>
  <BaseURL serviceLocation="b" dvb:priority="1" dvb:weight="2">http://video.com/baseUrl/b/</BaseURL>
  <BaseURL serviceLocation="c" dvb:priority="1" dvb:weight="3">http://video.com/baseUrl/c/</BaseURL>
  <Period start="PT0.000S">
    <BaseURL>media/</BaseURL>
    <BaseURL>files/</BaseURL>
    <AdaptationSet contentType="audio">
      <BaseURL>audio</BaseURL>
      <SegmentTemplate/>
      <Representation/>
    </AdaptationSet>
    <AdaptationSet contentType="video">
      <BaseURL>video</BaseURL>
      <BaseURL serviceLocation="d" dvb:priority="1" dvb:weight="4">http://video.com/baseUrl/d/alternative/</BaseURL>
      <SegmentTemplate/>
      <Representation/>
    </AdaptationSet>
    <AdaptationSet contentType="text">
      <BaseURL serviceLocation="e" dvb:priority="1" dvb:weight="1">http://video.com/baseUrl/e/text/</BaseURL>
      <SegmentTemplate/>
      <Representation/>
    </AdaptationSet>
  </Period>
</MPD>
)mpd";
    constexpr std::string_view kRelativeBaseUrlsDvbProfileNotDeclared = R"mpd(<?xml version="1.0" encoding="UTF-8"?>
<MPD xmlns:xsi="http://www.w3.org/2001/XMLSchema-instance"
  xmlns="urn:mpeg:DASH:schema:MPD:2011"
  xsi:schemaLocation="urn:mpeg:DASH:schema:MPD:2011 DASH-MPD.xsd"
  profiles="urn:mpeg:dash:profile:isoff-main:2011"
  type="dynamic"
  availabilityStartTime="2016-10-14T17:00:17">
  <Period start="PT0.000S">
    <BaseURL>//anotherhost.com/some/url/1/</BaseURL>
    <BaseURL>//anotherhost.com/some/url/2/</BaseURL>
    <AdaptationSet contentType="audio">
      <SegmentTemplate/>
      <Representation/>
    </AdaptationSet>
  </Period>
</MPD>
)mpd";
    constexpr std::string_view kRelativeBaseUrlsDvbProfileDeclared = R"mpd(<?xml version="1.0" encoding="UTF-8"?>
<MPD xmlns:xsi="http://www.w3.org/2001/XMLSchema-instance"
  xmlns="urn:mpeg:DASH:schema:MPD:2011"
  xsi:schemaLocation="urn:mpeg:DASH:schema:MPD:2011 DASH-MPD.xsd"
  xmlns:dvb="urn:dvb:dash:dash-extensions:2014-1"
  profiles="urn:mpeg:dash:profile:isoff-main:2011,urn:dvb:dash:profile:dvb-dash:2014"
  type="dynamic"
  availabilityStartTime="2016-10-14T17:00:17">
  <Period start="PT0.000S">
    <BaseURL>//anotherhost.com/some/url/1/</BaseURL>
    <BaseURL>//anotherhost.com/some/url/2/</BaseURL>
    <AdaptationSet contentType="audio">
      <SegmentTemplate/>
      <Representation/>
    </AdaptationSet>
  </Period>
</MPD>
)mpd";
    constexpr std::string_view kAvailabilityTimeOffsetSegmentTemplate = R"mpd(<?xml version="1.0" encoding="UTF-8"?>
<MPD xmlns:xsi="http://www.w3.org/2001/XMLSchema-instance"
  xmlns="urn:mpeg:DASH:schema:MPD:2011"
  xsi:schemaLocation="urn:mpeg:DASH:schema:MPD:2011 DASH-MPD.xsd"
  profiles="urn:mpeg:dash:profile:isoff-main:2011"
  type="dynamic"
  availabilityStartTime="2016-10-14T17:00:17">
  <BaseURL availabilityTimeOffset="9.999">http://video.com/baseUrl</BaseURL>
  <Period start="PT0.000S">
    <BaseURL availabilityTimeOffset="9.999">http://video.com/baseUrl</BaseURL>
    <SegmentTemplate availabilityTimeOffset="2"/>
    <AdaptationSet>
      <Representation>
        <BaseURL availabilityTimeOffset="9.999">http://video.com/baseUrl</BaseURL>
        <SegmentTemplate/>
      </Representation>
    </AdaptationSet>
    <AdaptationSet>
      <BaseURL availabilityTimeOffset="9.999">http://video.com/baseUrl</BaseURL>
      <SegmentTemplate availabilityTimeOffset="3.21"/>
      <Representation/>
    </AdaptationSet>
    <AdaptationSet>
      <SegmentTemplate/>
      <Representation>
        <BaseURL availabilityTimeOffset="9.999">http://video.com/baseUrl</BaseURL>
        <SegmentTemplate availabilityTimeOffset="1.23"/>
      </Representation>
    </AdaptationSet>
    <AdaptationSet>
      <BaseURL availabilityTimeOffset="9.999">http://video.com/baseUrl</BaseURL>
      <SegmentTemplate availabilityTimeOffset="123"/>
      <Representation>
        <BaseURL availabilityTimeOffset="9.999">http://video.com/baseUrl</BaseURL>
        <SegmentTemplate availabilityTimeOffset="0.1"/>
      </Representation>
    </AdaptationSet>
  </Period>
  <Period start="PT200.000S">
    <AdaptationSet>
      <Representation>
        <SegmentTemplate/>
      </Representation>
    </AdaptationSet>
  </Period>
</MPD>
)mpd";
    constexpr std::string_view kAvailabilityTimeOffsetSegmentList = R"mpd(<?xml version="1.0" encoding="UTF-8"?>
<MPD xmlns:xsi="http://www.w3.org/2001/XMLSchema-instance"
  xmlns="urn:mpeg:DASH:schema:MPD:2011"
  xsi:schemaLocation="urn:mpeg:DASH:schema:MPD:2011 DASH-MPD.xsd"
  profiles="urn:mpeg:dash:profile:isoff-main:2011"
  type="dynamic"
  availabilityStartTime="2016-10-14T17:00:17">
  <BaseURL availabilityTimeOffset="9.999">http://video.com/baseUrl</BaseURL>
  <Period start="PT0.000S">
    <BaseURL availabilityTimeOffset="9.999">http://video.com/baseUrl</BaseURL>
    <SegmentList availabilityTimeOffset="2"/>
    <AdaptationSet>
      <Representation>
        <BaseURL availabilityTimeOffset="9.999">http://video.com/baseUrl</BaseURL>
        <SegmentList/>
      </Representation>
    </AdaptationSet>
    <AdaptationSet>
      <BaseURL availabilityTimeOffset="9.999">http://video.com/baseUrl</BaseURL>
      <SegmentList availabilityTimeOffset="3.21"/>
      <Representation/>
    </AdaptationSet>
    <AdaptationSet>
      <SegmentList/>
      <Representation>
        <BaseURL availabilityTimeOffset="9.999">http://video.com/baseUrl</BaseURL>
        <SegmentList availabilityTimeOffset="1.23"/>
      </Representation>
    </AdaptationSet>
    <AdaptationSet>
      <BaseURL availabilityTimeOffset="9.999">http://video.com/baseUrl</BaseURL>
      <SegmentList availabilityTimeOffset="123"/>
      <Representation>
        <BaseURL availabilityTimeOffset="9.999">http://video.com/baseUrl</BaseURL>
        <SegmentList availabilityTimeOffset="0.1"/>
      </Representation>
    </AdaptationSet>
  </Period>
  <Period start="PT200.000S">
    <AdaptationSet>
      <Representation>
        <SegmentList/>
      </Representation>
    </AdaptationSet>
  </Period>
</MPD>
)mpd";
    constexpr std::string_view kServiceDescriptionLowLatency = R"mpd(<?xml version="1.0" encoding="UTF-8"?>
<MPD type="static" duration="1s" mediaPresentationDuration="PT1S">
 <ServiceDescription>
  <Latency min="1000" target="20000" max="30000"/>
  <PlaybackRate min="0.1" max="99"/>
 </ServiceDescription>
 <Period>
  <AdaptationSet id="0" mimeType="audio/mp4" subsegmentAlignment="true">
   <Representation id="0" codecs="mp4a.40.2" audioSamplingRate="48000" bandwidth="144000">
    <BaseURL>https://test.com/0</BaseURL>
   </Representation>
  </AdaptationSet>
 </Period>
</MPD>
)mpd";
    constexpr std::string_view kServiceDescriptionLowLatencyOnlyPlaybackRates = R"mpd(<?xml version="1.0" encoding="UTF-8"?>
<MPD type="static" duration="1s" mediaPresentationDuration="PT1S">
 <ServiceDescription>
  <PlaybackRate min="0.1" max="99"/>
 </ServiceDescription>
 <Period>
  <AdaptationSet id="0" mimeType="audio/mp4" subsegmentAlignment="true">
   <Representation id="0" codecs="mp4a.40.2" audioSamplingRate="48000" bandwidth="144000">
    <BaseURL>https://test.com/0</BaseURL>
   </Representation>
  </AdaptationSet>
 </Period>
</MPD>
)mpd";
    constexpr std::string_view kServiceDescriptionLowLatencyOnlyTargetLatency = R"mpd(<?xml version="1.0" encoding="UTF-8"?>
<MPD type="static" duration="1s" mediaPresentationDuration="PT1S">
 <ServiceDescription>
  <Latency target="20000"/>
 </ServiceDescription>
 <Period>
  <AdaptationSet id="0" mimeType="audio/mp4" subsegmentAlignment="true">
   <Representation id="0" codecs="mp4a.40.2" audioSamplingRate="48000" bandwidth="144000">
    <BaseURL>https://test.com/0</BaseURL>
   </Representation>
  </AdaptationSet>
 </Period>
</MPD>
)mpd";
    constexpr std::string_view kClearKeyLicenseUrl = R"mpd(<?xml version="1.0" encoding="UTF-8"?>
<!--
Includes ContentProtection elements with additional ClearKey license URLs.
Covers all possible locations (in AdaptationSet and Representation) and possible orders of these
ContentProtection elements (CENC first or ClearKey first).
-->
<MPD xmlns:xsi="http://www.w3.org/2001/XMLSchema-instance" xmlns="urn:mpeg:DASH:schema:MPD:2011" xsi:schemaLocation="urn:mpeg:DASH:schema:MPD:2011 DASH-MPD.xsd" minBufferTime="PT1.500S" profiles="urn:mpeg:dash:profile:isoff-main:2011" type="static" availabilityStartTime="2016-10-14T17:00:17" xmlns:cenc="urn:mpeg:cenc:2013" xmlns:clearkey="http://dashif.org/guidelines/clearKey">
 <Period start="PT0.000S" duration="PT0H5M50S">
  <SegmentTemplate startNumber="0" timescale="1000" media="sq/$Number$">
   <SegmentTimeline>
    <S d="2002" t="6009" r="2"/>
   </SegmentTimeline>
  </SegmentTemplate>
  <AdaptationSet id="0" mimeType="audio/mp4" subsegmentAlignment="true">
   <Representation id="140" codecs="mp4a.40.2" audioSamplingRate="48000" startWithSAP="1" bandwidth="144000">
     <ContentProtection schemeIdUri="urn:mpeg:dash:mp4protection:2011" value="cenc" cenc:default_KID="9eb4050d-e44b-4802-932e-27d75083e266" />
     <ContentProtection value="ClearKey1.0" schemeIdUri="urn:uuid:e2719d58-a985-b3c9-781a-b030af78d30e">
      <clearkey:Laurl Lic_type="EME-1.0">https://testserver1.test/AcquireLicense</clearkey:Laurl>
     </ContentProtection>
   </Representation>
  </AdaptationSet>
  <AdaptationSet id="1" mimeType="video/mp4" subsegmentAlignment="true">
   <ContentProtection value="ClearKey1.0" schemeIdUri="urn:uuid:e2719d58-a985-b3c9-781a-b030af78d30e">
    <clearkey:Laurl Lic_type="EME-1.0">https://testserver2.test/AcquireLicense</clearkey:Laurl>
   </ContentProtection>
   <ContentProtection schemeIdUri="urn:mpeg:dash:mp4protection:2011" value="cenc" cenc:default_KID="9eb4050d-e44b-4802-932e-27d75083e266" />
   <Representation id="133" codecs="avc1.4d4015" width="426" height="240" startWithSAP="1" bandwidth="258000" frameRate="30" />
  </AdaptationSet>
 </Period>
</MPD>
)mpd";
    constexpr std::string_view kDashifLicenseUrl = R"mpd(<?xml version="1.0" encoding="UTF-8"?>
<!--
Includes ContentProtection elements with additional "dashif:Laurl" license URLs.
Covers all possible locations (in AdaptationSet and Representation) and possible order of the
licence URL and the cenc value.
-->
<MPD xmlns:xsi="http://www.w3.org/2001/XMLSchema-instance" xmlns="urn:mpeg:DASH:schema:MPD:2011" xsi:schemaLocation="urn:mpeg:DASH:schema:MPD:2011 DASH-MPD.xsd" minBufferTime="PT1.500S" profiles="urn:mpeg:dash:profile:isoff-main:2011" type="static" availabilityStartTime="2016-10-14T17:00:17" xmlns:cenc="urn:mpeg:cenc:2013" xmlns:clearkey="http://dashif.org/guidelines/clearKey">
 <Period start="PT0.000S" duration="PT0H5M50S">
  <SegmentTemplate startNumber="0" timescale="1000" media="sq/$Number$">
   <SegmentTimeline>
    <S d="2002" t="6009" r="2"/>
   </SegmentTimeline>
  </SegmentTemplate>
  <AdaptationSet id="0" mimeType="audio/mp4" subsegmentAlignment="true">
   <Representation id="140" codecs="mp4a.40.2" audioSamplingRate="48000" startWithSAP="1" bandwidth="144000">
     <ContentProtection schemeIdUri="urn:mpeg:dash:mp4protection:2011" value="cenc" cenc:default_KID="30303030-3030-3030-3030-303030303030" />
     <ContentProtection value="Widevine" schemeIdUri="urn:uuid:edef8ba9-79d6-4ace-a3c8-27dcd51d21ed">
      <cenc:pssh>AAAANHBzc2gAAAAA7e+LqXnWSs6jyCfc1R0h7QAAABQIARIQnrQFDeRLSAKTLifXUIPiZg==</cenc:pssh>
      <dashif:Laurl>https://testserver1.test/AcquireLicense</dashif:Laurl>
     </ContentProtection>
   </Representation>
  </AdaptationSet>
  <AdaptationSet id="1" mimeType="video/mp4" subsegmentAlignment="true">
   <ContentProtection value="Widevine" schemeIdUri="urn:uuid:edef8ba9-79d6-4ace-a3c8-27dcd51d21ed">
     <dashif:Laurl>https://testserver2.test/AcquireLicense</dashif:Laurl>
     <cenc:pssh>AAAANHBzc2gAAAAA7e+LqXnWSs6jyCfc1R0h7QAAABQIARIQnrQFDeRLSAKTLifXUIPiZg==</cenc:pssh>
   </ContentProtection>
   <ContentProtection schemeIdUri="urn:mpeg:dash:mp4protection:2011" value="cenc" cenc:default_KID="30303030-3030-3030-3030-303030303030" />
   <Representation id="133" codecs="avc1.4d4015" width="426" height="240" startWithSAP="1" bandwidth="258000" frameRate="30" />
  </AdaptationSet>
 </Period>
</MPD>
)mpd";
    constexpr std::string_view kDolby = R"mpd(<?xml version="1.0" encoding="UTF-8"?>
<MPD xmlns="urn:mpeg:dash:schema:mpd:2011" 
     profiles="urn:mpeg:dash:profile:isoff-live:2011,http://dashif.org/guidelines/dash-if-simple-2017" 
     type="static" 
     mediaPresentationDuration="PT0H10M0.000S" 
     minBufferTime="PT2.0S">
  <Period id="p0" duration="PT0H10M0.000S">
    <AdaptationSet id="0" contentType="video" mimeType="video/mp4" segmentAlignment="true" maxWidth="3840" maxHeight="2160" maxFrameRate="60">
      <SupplementalProperty schemeIdUri="urn:dolby:dash:dolby-vision:2018" value="10.1"/> 
      <Representation id="dv_4k_15mbps" 
                      bandwidth="15000000" 
                      codecs="dvh1.10.05" 
                      width="3840" 
                      height="2160"
                      frameRate="60">
        <SegmentTemplate timescale="24000" 
                         initialization="video/init.mp4" 
                         media="video/seg_$Number$.m4s" 
                         startNumber="1" 
                         duration="96000"/>
      </Representation>
      <Representation id="dv_1080p_5mbps"
                      bandwidth="5000000"
                      codecs="dvh1.10.02"
                      width="1920"
                      height="1080"
                      frameRate="24">
        <SegmentTemplate timescale="24000"
                         initialization="video_low/init.mp4"
                         media="video_low/seg_$Number$.m4s"
                         startNumber="1"
                         duration="96000"/>
      </Representation>
    </AdaptationSet>
  </Period>
</MPD>
)mpd";
    constexpr std::string_view kSupplementalCodecs = R"mpd(<?xml version="1.0" encoding="UTF-8"?>
<MPD xmlns:xsi="http://www.w3.org/2001/XMLSchema-instance"
  xmlns="urn:mpeg:dash:schema:mpd:2011"
  xmlns:scte214="urn:scte:dash:scte214-extensions"
  xsi:schemaLocation="urn:mpeg:dash:schema:mpd:2011"
  minBufferTime="PT1S"
  profiles="urn:mpeg:dash:profile:isoff-main:2011"
  type="static"
  mediaPresentationDuration="PT904S">
  <Period id="1" duration="PT904S" start="PT0S">
    <AdaptationSet id="3000000000" mimeType="video/mp4" contentType="video" segmentAlignment="true" startWithSAP="1">
      <SegmentTemplate presentationTimeOffset="0" media="video_$Time$_$Bandwidth$.m4s" timescale="1000" >
        <SegmentTimeline>
          <S d="4000" r="225"/>
        </SegmentTimeline>
      </SegmentTemplate>
      <Representation id="0" codecs="hvc1.2.4.L123.90" width="768" height="432" frameRate="25" bandwidth="1300000" scte214:supplementalCodecs="dvh1.08.03" scte214:supplementalProfiles="db1p" />
    </AdaptationSet>
  </Period>
</MPD>
)mpd";
    constexpr std::string_view kMultipleLocationsRelative = R"mpd(<?xml version="1.0" encoding="utf-8"?>
<MPD xmlns="urn:mpeg:dash:schema:mpd:2011" type="dynamic"
     availabilityStartTime="2026-05-14T12:00:00Z"
     minimumUpdatePeriod="PT1S" minBufferTime="PT1S">
  <Location>location1/manifest.mpd</Location>
  <Location serviceLocation="loc2">location2/manifest.mpd</Location>
  <Period id="1" start="PT0S">
    <AdaptationSet mimeType="video/mp4">
      <Representation id="1" bandwidth="1" width="1" height="1" />
    </AdaptationSet>
  </Period>
</MPD>
)mpd";
    constexpr std::string_view kMultipleLocationsAbsolute = R"mpd(<?xml version="1.0" encoding="utf-8"?>
<MPD xmlns="urn:mpeg:dash:schema:mpd:2011" type="dynamic"
     availabilityStartTime="2026-05-14T12:00:00Z"
     minimumUpdatePeriod="PT1S" minBufferTime="PT1S">
  <Location>https://example.com/location1/manifest.mpd</Location>
  <Location serviceLocation="loc2">https://example.com/location2/manifest.mpd</Location>
  <Period id="1" start="PT0S">
    <AdaptationSet mimeType="video/mp4">
      <Representation id="1" bandwidth="1" width="1" height="1" />
    </AdaptationSet>
  </Period>
</MPD>
)mpd";

}
