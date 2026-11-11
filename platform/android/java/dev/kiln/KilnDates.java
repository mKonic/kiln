package dev.kiln;

import android.icu.text.DateFormat;
import android.icu.text.DisplayContext;
import android.icu.util.TimeZone;

import java.time.DayOfWeek;
import java.time.LocalDate;
import java.time.chrono.Chronology;
import java.time.format.DateTimeFormatter;
import java.time.format.DateTimeFormatterBuilder;
import java.time.format.DateTimeParseException;
import java.time.format.DecimalStyle;
import java.time.format.FormatStyle;
import java.time.format.TextStyle;
import java.time.temporal.WeekFields;
import java.util.HashMap;
import java.util.Locale;

/** Dates for kiln's date pickers, as Compose's CalendarModelImpl formats and parses them. */
final class KilnDates {
    private static final HashMap<String, Object> sCache = new HashMap<>();

    private KilnDates() {}

    private static Locale locale(String tag) { return Locale.forLanguageTag(tag); }

    /** formatWithSkeleton: ICU's best pattern for the skeleton, standalone capitalisation, GMT. */
    static String formatSkeleton(long utcMillis, String skeleton, String tag) {
        String key = "S:" + skeleton + tag;
        DateFormat f = (DateFormat) sCache.get(key);
        if (f == null) {
            f = DateFormat.getInstanceForSkeleton(skeleton, locale(tag));
            f.setContext(DisplayContext.CAPITALIZATION_FOR_STANDALONE);
            f.setTimeZone(TimeZone.GMT_ZONE);
            sCache.put(key, f);
        }
        return f.format(new java.util.Date(utcMillis));
    }

    private static DateTimeFormatter formatter(String pattern, String tag) {
        String key = "P:" + pattern + tag;
        DateTimeFormatter f = (DateTimeFormatter) sCache.get(key);
        if (f == null) {
            Locale l = locale(tag);
            f = DateTimeFormatter.ofPattern(pattern, l).withDecimalStyle(DecimalStyle.of(l));
            sCache.put(key, f);
        }
        return f;
    }

    static String formatPattern(long utcMillis, String pattern, String tag) {
        return LocalDate.ofEpochDay(Math.floorDiv(utcMillis, 86400000L)).format(formatter(pattern, tag));
    }

    /** LocalDate.parse: {year, month, day}, or null. */
    static int[] parse(String text, String pattern, String tag) {
        try {
            LocalDate d = LocalDate.parse(text, formatter(pattern, tag));
            return new int[] { d.getYear(), d.getMonthValue(), d.getDayOfMonth() };
        } catch (DateTimeParseException e) {
            return null;
        }
    }

    static String shortDatePattern(String tag) {
        Locale l = locale(tag);
        return DateTimeFormatterBuilder.getLocalizedDateTimePattern(FormatStyle.SHORT, null, Chronology.ofLocale(l), l);
    }

    static int firstDayOfWeek(String tag) { return WeekFields.of(locale(tag)).getFirstDayOfWeek().getValue(); }

    /** Monday first, each day's full then narrow standalone name. */
    static String[] weekdayNames(String tag) {
        Locale l = locale(tag);
        String[] out = new String[14];
        int i = 0;
        for (DayOfWeek d : DayOfWeek.values()) {
            out[i++] = d.getDisplayName(TextStyle.FULL_STANDALONE, l);
            out[i++] = d.getDisplayName(TextStyle.NARROW_STANDALONE, l);
        }
        return out;
    }

    static int[] today() {
        LocalDate d = LocalDate.now();
        return new int[] { d.getYear(), d.getMonthValue(), d.getDayOfMonth() };
    }
}
