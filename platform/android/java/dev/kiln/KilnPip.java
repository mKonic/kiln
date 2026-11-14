package dev.kiln;

import android.app.Activity;
import android.app.PictureInPictureParams;
import android.content.Context;
import android.content.ContextWrapper;
import android.graphics.Rect;
import android.os.Build;
import android.util.Rational;
import android.view.View;

/** Picture-in-picture and the requested orientation for kiln's ui::PictureInPicture. */
final class KilnPip {
    private KilnPip() {}

    static Activity activityOf(View view) {
        Context c = view.getContext();
        while (c instanceof ContextWrapper) {
            if (c instanceof Activity) return (Activity) c;
            c = ((ContextWrapper) c).getBaseContext();
        }
        return null;
    }

    /** setPictureInPictureParams, or enterPictureInPictureMode when `enter`. */
    static boolean apply(View view, boolean enter, int aspectWidth, int aspectHeight, int[] hint, boolean autoEnter,
                         boolean seamlessResize, String title, String subtitle) {
        Activity activity = activityOf(view);
        if (activity == null) return false;
        try {
            PictureInPictureParams.Builder b = new PictureInPictureParams.Builder();
            if (aspectWidth > 0 && aspectHeight > 0) b.setAspectRatio(new Rational(aspectWidth, aspectHeight));
            if (hint != null) b.setSourceRectHint(new Rect(hint[0], hint[1], hint[2], hint[3]));
            b.setAutoEnterEnabled(autoEnter);
            b.setSeamlessResizeEnabled(seamlessResize);
            if (Build.VERSION.SDK_INT >= 33) {
                if (!title.isEmpty()) b.setTitle(title);
                if (!subtitle.isEmpty()) b.setSubtitle(subtitle);
            }
            PictureInPictureParams params = b.build();
            if (enter) return activity.enterPictureInPictureMode(params);
            activity.setPictureInPictureParams(params);
            return true;
        } catch (IllegalArgumentException | IllegalStateException e) {
            // An aspect ratio out of range, or an activity without supportsPictureInPicture.
            android.util.Log.w("kiln", "picture-in-picture: " + e.getMessage());
            return false;
        }
    }

    static void setRequestedOrientation(View view, int orientation) {
        Activity activity = activityOf(view);
        if (activity != null) activity.setRequestedOrientation(orientation);
    }

    static int requestedOrientation(View view) {
        Activity activity = activityOf(view);
        return activity != null ? activity.getRequestedOrientation() : -1;
    }

    static boolean isInPictureInPictureMode(View view) {
        Activity activity = activityOf(view);
        return activity != null && activity.isInPictureInPictureMode();
    }
}
