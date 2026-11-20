package dev.kiln;

import android.content.Context;
import android.content.Intent;
import android.content.pm.ShortcutInfo;
import android.content.pm.ShortcutManager;
import android.graphics.drawable.Icon;
import android.net.Uri;

import java.util.ArrayList;
import java.util.List;

/**
 * ui::Shortcuts on ShortcutManager. Shortcuts come as {id, shortLabel, longLabel, icon, rank,
 * data, intent spec or null, categories, persons, long-lived, locus id}; results as String[] ("ok", or the ids for a listing).
 */
final class KilnShortcuts {
    private static final String[] OK = { "ok" }, FAILED = {};

    private KilnShortcuts() {}

    static String[] run(Context context, int op, Object[] shortcuts, String[] ids) {
        ShortcutManager sm = context.getSystemService(ShortcutManager.class);
        if (sm == null) return FAILED;
        try {
            switch (op) {
                case 0: return sm.setDynamicShortcuts(infos(context, shortcuts)) ? OK : FAILED;
                case 1: {
                    // ShortcutManagerCompat.pushDynamicShortcut: nothing when the launcher takes
                    // no shortcuts, and the push counts as a use (for ranking).
                    if (sm.getMaxShortcutCountPerActivity() == 0) return FAILED;
                    ShortcutInfo info = infos(context, shortcuts).get(0);
                    sm.pushDynamicShortcut(info);
                    sm.reportShortcutUsed(info.getId());
                    return OK;
                }
                case 2: sm.removeDynamicShortcuts(java.util.Arrays.asList(ids)); return OK;
                case 3: sm.removeAllDynamicShortcuts(); return OK;
                case 4: sm.reportShortcutUsed(ids[0]); return OK;
                case 5:
                    if (!sm.isRequestPinShortcutSupported()) return FAILED;
                    return sm.requestPinShortcut(infos(context, shortcuts).get(0), null) ? OK : FAILED;
                case 7: sm.removeLongLivedShortcuts(java.util.Arrays.asList(ids)); return OK;
                case 6: {
                    List<ShortcutInfo> list = sm.getDynamicShortcuts();
                    String[] out = new String[list.size()];
                    for (int i = 0; i < out.length; ++i) out[i] = list.get(i).getId();
                    return out;
                }
                default: return FAILED;
            }
        } catch (IllegalArgumentException | IllegalStateException e) {
            // Too many shortcuts, a rate limit, or a shortcut the system refuses.
            android.util.Log.w("kiln", "shortcuts: " + e.getMessage());
            return FAILED;
        }
    }

    private static List<ShortcutInfo> infos(Context context, Object[] shortcuts) {
        List<ShortcutInfo> out = new ArrayList<>();
        for (Object o : shortcuts) {
            Object[] s = (Object[]) o;
            ShortcutInfo.Builder b = new ShortcutInfo.Builder(context, (String) s[0]);
            b.setShortLabel((String) s[1]);
            if (!((String) s[2]).isEmpty()) b.setLongLabel((String) s[2]);
            b.setIcon(icon(context, (String) s[3]));
            b.setRank(Integer.parseInt((String) s[4]));
            Intent intent;
            if (s[6] != null) {
                intent = KilnActivityResults.toIntent((Object[]) s[6]);
                if (intent.getAction() == null) intent.setAction(Intent.ACTION_VIEW);
            } else {
                // The app's launcher activity, opened at the shortcut's deep link.
                Intent launch = context.getPackageManager().getLaunchIntentForPackage(context.getPackageName());
                intent = new Intent(Intent.ACTION_VIEW);
                if (launch != null && launch.getComponent() != null) intent.setComponent(launch.getComponent());
                if (!((String) s[5]).isEmpty()) intent.setData(Uri.parse((String) s[5]));
            }
            b.setIntent(intent);
            // {String[] categories, Object[] persons, "true" long-lived, locus id} (ShortcutInfoCompat.toShortcutInfo).
            String[] categories = (String[]) s[7];
            if (categories.length > 0) b.setCategories(new java.util.HashSet<>(java.util.Arrays.asList(categories)));
            Object[] persons = (Object[]) s[8];
            if (persons.length > 0) {
                android.app.Person[] ps = new android.app.Person[persons.length];
                for (int i = 0; i < ps.length; ++i) ps[i] = KilnNotifications.person(context, (String[]) persons[i]);
                b.setPersons(ps);
            }
            if (!((String) s[10]).isEmpty()) b.setLocusId(new android.content.LocusId((String) s[10]));
            b.setLongLived("true".equals(s[9]));
            out.add(b.build());
        }
        return out;
    }

    private static Icon icon(Context context, String name) {
        int id = 0;
        if (!name.isEmpty()) {
            id = context.getResources().getIdentifier(name, "drawable", context.getPackageName());
            if (id == 0) id = context.getResources().getIdentifier(name, "mipmap", context.getPackageName());
        }
        if (id == 0) id = context.getApplicationInfo().icon;
        return Icon.createWithResource(context, id);
    }
}
