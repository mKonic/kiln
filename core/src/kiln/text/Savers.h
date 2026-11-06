#pragma once

// AnnotatedString.Saver (ui-text Savers.kt): the text with its span styles, paragraph styles,
// string, TTS and link annotations. Like Compose, a span's font family, brush and draw style
// are not kept, and links come back without their listeners.

#include "kiln/runtime/Saveable.h"
#include "kiln/text/AnnotatedString.h"

namespace kiln::text {

    rt::Saver<AnnotatedString> AnnotatedStringSaver();
    rt::Saver<SpanStyle> SpanStyleSaver();
    rt::Saver<ParagraphStyle> ParagraphStyleSaver();

}
