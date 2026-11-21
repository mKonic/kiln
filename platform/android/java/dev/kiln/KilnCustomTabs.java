package dev.kiln;

import android.app.PendingIntent;
import android.content.ComponentName;
import android.content.Context;
import android.content.Intent;
import android.content.ServiceConnection;
import android.content.pm.PackageManager;
import android.content.pm.ResolveInfo;
import android.net.Uri;
import android.os.Binder;
import android.os.Bundle;
import android.os.IBinder;
import android.os.Parcel;
import android.os.RemoteException;

import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;

/**
 * Custom Tabs' client side without androidx.browser (CustomTabsClient, CustomTabsSession,
 * CustomTabsServiceConnection): the browser's ICustomTabsService called through its binder with
 * the stable AIDL's transaction codes, and ICustomTabsCallback / IEngagementSignalsCallback
 * stubs that hand events to native. Bundles cross as KilnBundles' arrays.
 */
final class KilnCustomTabs {
    private KilnCustomTabs() {}

    static native void connected(int connection, boolean connected);
    static native void callback(int session, String name, Object[] args);
    static native Object[] callbackWithResult(int session, String name, Object[] args);
    static native void engagement(int callback, String name, Object[] args);

    private static final String ACTION_CUSTOM_TABS_CONNECTION = "android.support.customtabs.action.CustomTabsService";
    private static final String SERVICE = "android.support.customtabs.ICustomTabsService";
    private static final String CALLBACK = "android.support.customtabs.ICustomTabsCallback";
    private static final String ENGAGEMENT = "android.support.customtabs.IEngagementSignalsCallback";
    private static final String AUTH_CALLBACK = "android.support.customtabs.IAuthTabCallback";
    private static final String EXTRA_SESSION_ID = "android.support.customtabs.extra.SESSION_ID";
    // Stable AIDL: the explicit ids are offsets from FIRST_CALL_TRANSACTION.
    private static final int FIRST = IBinder.FIRST_CALL_TRANSACTION;
    private static final int GET_INTERFACE_VERSION = 0x00fffffe, GET_INTERFACE_HASH = 0x00fffffd;

    private static final class Connection implements ServiceConnection {
        final int id;
        IBinder service;
        ComponentName component;
        Connection(int id) { this.id = id; }
        @Override public void onServiceConnected(ComponentName name, IBinder binder) {
            service = binder;
            component = name;
            connected(id, true);
        }
        @Override public void onServiceDisconnected(ComponentName name) {
            service = null;
            connected(id, false);
        }
        @Override public void onBindingDied(ComponentName name) { onServiceDisconnected(name); }
    }

    private static final class Session {
        final Connection connection;
        final Binder callback;
        final PendingIntent sessionId;
        Session(Connection c, Binder cb, PendingIntent id) { connection = c; callback = cb; sessionId = id; }
    }

    private static final HashMap<Integer, Connection> sConnections = new HashMap<>();
    private static final HashMap<Integer, Session> sSessions = new HashMap<>();
    private static int sNextId = 0;

    private static Context context() { return KilnContent.context(); }

    /** bindCustomTabsService (BIND_WAIVE_PRIORITY unless preserving priority): the id, 0 when it fails. */
    static synchronized int bind(String packageName, boolean preservePriority) {
        Context c = context();
        if (c == null || packageName.isEmpty()) return 0;
        Intent intent = new Intent(ACTION_CUSTOM_TABS_CONNECTION).setPackage(packageName);
        Connection connection = new Connection(++sNextId);
        int flags = Context.BIND_AUTO_CREATE | (preservePriority ? 0 : Context.BIND_WAIVE_PRIORITY);
        try {
            if (!c.bindService(intent, connection, flags)) return 0;
        } catch (SecurityException e) {
            return 0;
        }
        sConnections.put(connection.id, connection);
        return connection.id;
    }

    static synchronized void unbind(int id) {
        Connection connection = sConnections.remove(id);
        Context c = context();
        if (connection != null && c != null) {
            try { c.unbindService(connection); } catch (IllegalArgumentException ignored) {}
        }
    }

    /** CustomTabsClient.getPackageName: the default browser first unless ignored, then `packages`. */
    @SuppressWarnings("deprecation")
    static String packageName(String[] packages, boolean ignoreDefault) {
        Context c = context();
        if (c == null) return null;
        PackageManager pm = c.getPackageManager();
        ArrayList<String> names = new ArrayList<>();
        if (!ignoreDefault) {
            ResolveInfo info = pm.resolveActivity(new Intent(Intent.ACTION_VIEW, Uri.parse("http://")), 0);
            if (info != null) names.add(info.activityInfo.packageName);
        }
        for (String p : packages) names.add(p);
        Intent service = new Intent(ACTION_CUSTOM_TABS_CONNECTION);
        for (String name : names) {
            service.setPackage(name);
            if (pm.resolveService(service, 0) != null) return name;
        }
        return null;
    }

    /** packageHasCategory: the provider's service declares the category. */
    static boolean packageHasCategory(String provider, String category) {
        Context c = context();
        if (c == null) return false;
        List<ResolveInfo> services = c.getPackageManager().queryIntentServices(new Intent(ACTION_CUSTOM_TABS_CONNECTION), PackageManager.GET_RESOLVED_FILTER);
        for (ResolveInfo service : services) {
            if (service.serviceInfo != null && provider.equals(service.serviceInfo.packageName) && service.filter != null && service.filter.hasCategory(category)) return true;
        }
        return false;
    }

    private static IBinder service(int connection) {
        synchronized (KilnCustomTabs.class) {
            Connection c = sConnections.get(connection);
            return c == null ? null : c.service;
        }
    }

    private static void writeBundle(Parcel p, Bundle b) {
        if (b == null) { p.writeInt(0); return; }
        p.writeInt(1);
        b.writeToParcel(p, 0);
    }
    private static void writeUri(Parcel p, String uri) {
        if (uri == null || uri.isEmpty()) { p.writeInt(0); return; }
        p.writeInt(1);
        Uri.parse(uri).writeToParcel(p, 0);
    }
    private static Bundle readBundle(Parcel p) { return p.readInt() != 0 ? Bundle.CREATOR.createFromParcel(p) : null; }
    private static String readUri(Parcel p) { return p.readInt() != 0 ? Uri.CREATOR.createFromParcel(p).toString() : ""; }

    private interface Writer { void write(Parcel p); }

    /** One call to the service: the reply's int (a boolean or an int), -2 (remote error) on failure. */
    private static int transactInt(IBinder service, int code, Writer writer) {
        if (service == null) return -2;
        Parcel data = Parcel.obtain(), reply = Parcel.obtain();
        try {
            data.writeInterfaceToken(SERVICE);
            writer.write(data);
            if (!service.transact(FIRST + code, data, reply, 0)) return -2;
            reply.readException();
            return reply.readInt();
        } catch (RemoteException | RuntimeException e) {
            return -2;
        } finally {
            data.recycle();
            reply.recycle();
        }
    }

    static boolean warmup(int connection, long flags) {
        return transactInt(service(connection), 1, p -> p.writeLong(flags)) == 1;
    }

    /** extraCommand: the reply's Bundle as arrays, or null. */
    static Object[] extraCommand(int connection, String name, Object[] args) {
        IBinder service = service(connection);
        if (service == null) return null;
        Parcel data = Parcel.obtain(), reply = Parcel.obtain();
        try {
            data.writeInterfaceToken(SERVICE);
            data.writeString(name);
            writeBundle(data, KilnBundles.toBundle(args));
            if (!service.transact(FIRST + 4, data, reply, 0)) return null;
            reply.readException();
            Bundle b = readBundle(reply);
            return b == null ? null : KilnBundles.fromBundle(b);
        } catch (RemoteException | RuntimeException e) {
            return null;
        } finally {
            data.recycle();
            reply.recycle();
        }
    }

    private static PendingIntent sessionIdIntent(int id) {
        // createSessionId: an empty intent, only an identifier.
        return PendingIntent.getActivity(context(), id, new Intent(), PendingIntent.FLAG_IMMUTABLE);
    }

    /** newSession / newSessionWithExtras (with a session id): the session's id, 0 when refused. */
    static int newSession(int connection, int sessionId, boolean hasSessionId) {
        Connection c;
        synchronized (KilnCustomTabs.class) { c = sConnections.get(connection); }
        if (c == null || c.service == null) return 0;
        final int id;
        synchronized (KilnCustomTabs.class) { id = ++sNextId; }
        Binder callback = callbackStub(id);
        final PendingIntent pending = hasSessionId ? sessionIdIntent(sessionId) : null;
        int ok;
        if (pending != null) {
            Bundle extras = new Bundle();
            extras.putParcelable(EXTRA_SESSION_ID, pending);
            ok = transactInt(c.service, 9, p -> { p.writeStrongBinder(callback); writeBundle(p, extras); });
        } else {
            ok = transactInt(c.service, 2, p -> p.writeStrongBinder(callback));
        }
        if (ok != 1) return 0;
        synchronized (KilnCustomTabs.class) { sSessions.put(id, new Session(c, callback, pending)); }
        return id;
    }

    /** newAuthTabSession: an Auth Tab session (its callbacks named as a Custom Tabs session's). */
    static int newAuthTabSession(int connection, int sessionId, boolean hasSessionId) {
        Connection c;
        synchronized (KilnCustomTabs.class) { c = sConnections.get(connection); }
        if (c == null || c.service == null) return 0;
        final int id;
        synchronized (KilnCustomTabs.class) { id = ++sNextId; }
        Binder callback = authCallbackStub(id);
        final PendingIntent pending = hasSessionId ? sessionIdIntent(sessionId) : null;
        Bundle extras = new Bundle();
        if (pending != null) extras.putParcelable(EXTRA_SESSION_ID, pending);
        int ok = transactInt(c.service, 17, p -> { p.writeStrongBinder(callback); writeBundle(p, extras); });
        if (ok != 1) return 0;
        synchronized (KilnCustomTabs.class) { sSessions.put(id, new Session(c, callback, pending)); }
        return id;
    }

    /** EXTRA_SESSION for an intent: the session's callback binder. */
    static IBinder sessionBinder(int session) {
        synchronized (KilnCustomTabs.class) {
            Session s = sSessions.get(session);
            return s == null ? null : s.callback;
        }
    }
    static String sessionPackage(int session) {
        synchronized (KilnCustomTabs.class) {
            Session s = sSessions.get(session);
            return s == null || s.connection.component == null ? "" : s.connection.component.getPackageName();
        }
    }
    static PendingIntent pendingSessionId(int id) { return sessionIdIntent(id); }

    private static Bundle withId(Session s, Bundle b) {
        Bundle out = new Bundle();
        if (b != null) out.putAll(b);
        if (s.sessionId != null) out.putParcelable(EXTRA_SESSION_ID, s.sessionId);
        return out;
    }

    /**
     * A CustomTabsSession call by name, its arguments in a bundle (url, extras, others, urls,
     * options, icon, description, id, origin, targetOrigin, message, relation, purpose, engagement);
     * the result as {"result": int}, or {"unsupported": true} when the browser refuses the API.
     */
    @SuppressWarnings("deprecation")
    static Object[] sessionCall(int session, String method, Object[] argArrays) {
        Session s;
        synchronized (KilnCustomTabs.class) { s = sSessions.get(session); }
        Bundle result = new Bundle();
        if (s == null || s.connection.service == null) {
            result.putInt("result", method.equals("postMessage") ? -2 : 0);
            return KilnBundles.fromBundle(result);
        }
        Bundle a = KilnBundles.toBundle(argArrays);
        IBinder service = s.connection.service;
        Binder cb = s.callback;
        int r;
        switch (method) {
            case "mayLaunchUrl": {
                ArrayList<Bundle> others = a.getParcelableArrayList("others");
                r = transactInt(service, 3, p -> {
                    p.writeStrongBinder(cb);
                    writeUri(p, a.getString("url"));
                    writeBundle(p, withId(s, a.getBundle("extras")));
                    p.writeTypedList(others);
                });
                break;
            }
            case "prefetch": {
                Bundle options = withId(s, a.getBundle("options"));
                ArrayList<String> urls = a.getStringArrayList("urls");
                if (urls != null) {
                    ArrayList<Uri> list = new ArrayList<>();
                    for (String u : urls) list.add(Uri.parse(u));
                    r = transactInt(service, 15, p -> { p.writeStrongBinder(cb); p.writeTypedList(list); writeBundle(p, options); });
                } else {
                    r = transactInt(service, 14, p -> { p.writeStrongBinder(cb); writeUri(p, a.getString("url")); writeBundle(p, options); });
                }
                break;
            }
            case "updateVisuals":
                r = transactInt(service, 5, p -> { p.writeStrongBinder(cb); writeBundle(p, withIdIf(s, a.getBundle("bundle"), a.getBoolean("idInBundle"))); });
                break;
            case "requestPostMessageChannel": {
                Bundle extra = new Bundle();
                String target = a.getString("targetOrigin");
                if (target != null && !target.isEmpty()) extra.putParcelable("target_origin", Uri.parse(target));
                if (s.sessionId != null) extra.putParcelable(EXTRA_SESSION_ID, s.sessionId);
                if (!extra.isEmpty()) {
                    Bundle extras = a.getBundle("extras") != null ? a.getBundle("extras") : new Bundle();
                    extras.putAll(extra);
                    r = transactInt(service, 10, p -> { p.writeStrongBinder(cb); writeUri(p, a.getString("origin")); writeBundle(p, extras); });
                } else {
                    r = transactInt(service, 6, p -> { p.writeStrongBinder(cb); writeUri(p, a.getString("origin")); });
                }
                break;
            }
            case "postMessage":
                synchronized (s) {
                    r = transactInt(service, 7, p -> { p.writeStrongBinder(cb); p.writeString(a.getString("message")); writeBundle(p, withId(s, a.getBundle("extras"))); });
                }
                break;
            case "validateRelationship":
                r = transactInt(service, 8, p -> {
                    p.writeStrongBinder(cb);
                    p.writeInt(a.getInt("relation"));
                    writeUri(p, a.getString("origin"));
                    writeBundle(p, withId(s, a.getBundle("extras")));
                });
                break;
            case "receiveFile":
                r = transactInt(service, 11, p -> {
                    p.writeStrongBinder(cb);
                    writeUri(p, a.getString("uri"));
                    p.writeInt(a.getInt("purpose"));
                    writeBundle(p, withId(s, a.getBundle("extras")));
                });
                break;
            case "isEngagementSignalsApiAvailable":
                r = transactUnsupported(service, 12, p -> { p.writeStrongBinder(cb); writeBundle(p, withId(s, a.getBundle("extras"))); });
                break;
            case "setEngagementSignalsCallback": {
                Binder engagement = engagementStub(a.getInt("engagement"));
                r = transactUnsupported(service, 13, p -> { p.writeStrongBinder(cb); p.writeStrongBinder(engagement); writeBundle(p, withId(s, a.getBundle("extras"))); });
                break;
            }
            default:
                r = 0;
        }
        if (r == Integer.MIN_VALUE) result.putBoolean("unsupported", true);
        else result.putInt("result", r);
        return KilnBundles.fromBundle(result);
    }

    private static Bundle withIdIf(Session s, Bundle b, boolean add) { return add ? withId(s, b) : (b != null ? b : new Bundle()); }

    /** A call the browser may refuse with a SecurityException: MIN_VALUE then. */
    private static int transactUnsupported(IBinder service, int code, Writer writer) {
        Parcel data = Parcel.obtain(), reply = Parcel.obtain();
        try {
            data.writeInterfaceToken(SERVICE);
            writer.write(data);
            if (!service.transact(FIRST + code, data, reply, 0)) return 0;
            reply.readException();
            return reply.readInt();
        } catch (SecurityException e) {
            return Integer.MIN_VALUE;
        } catch (RemoteException | RuntimeException e) {
            return 0;
        } finally {
            data.recycle();
            reply.recycle();
        }
    }

    private static Object[] arrays(Bundle b) { return KilnBundles.fromBundle(b != null ? b : new Bundle()); }

    /** ICustomTabsCallback.Stub: events to native (which runs them on the main thread). */
    private static Binder callbackStub(int session) {
        return new Binder() {
            @Override public String getInterfaceDescriptor() { return CALLBACK; }
            @Override protected boolean onTransact(int code, Parcel data, Parcel reply, int flags) throws RemoteException {
                if (code == INTERFACE_TRANSACTION) { reply.writeString(CALLBACK); return true; }
                if (code == GET_INTERFACE_VERSION) { data.enforceInterface(CALLBACK); reply.writeNoException(); reply.writeInt(1); return true; }
                if (code == GET_INTERFACE_HASH) { data.enforceInterface(CALLBACK); reply.writeNoException(); reply.writeString("notfrozen"); return true; }
                if (code < FIRST + 1 || code > FIRST + 11) return super.onTransact(code, data, reply, flags);
                data.enforceInterface(CALLBACK);
                Bundle args = new Bundle();
                String name;
                switch (code - FIRST) {
                    case 1: name = "onNavigationEvent"; args.putInt("event", data.readInt()); args.putBundle("extras", readBundle(data)); break;
                    case 2: name = "extraCallback"; args.putString("name", data.readString()); args.putBundle("extras", readBundle(data)); break;
                    case 3: name = "onMessageChannelReady"; args.putBundle("extras", readBundle(data)); break;
                    case 4: name = "onPostMessage"; args.putString("message", data.readString()); args.putBundle("extras", readBundle(data)); break;
                    case 5:
                        name = "onRelationshipValidationResult";
                        args.putInt("relation", data.readInt());
                        args.putString("origin", readUri(data));
                        args.putBoolean("result", data.readInt() != 0);
                        args.putBundle("extras", readBundle(data));
                        break;
                    case 6: {
                        String callbackName = data.readString();
                        Bundle in = readBundle(data);
                        Bundle callArgs = new Bundle();
                        callArgs.putBundle("extras", in);
                        Object[] out = callbackWithResult(session, callbackName, arrays(callArgs));
                        reply.writeNoException();
                        writeBundle(reply, out == null ? null : KilnBundles.toBundle(out));
                        return true;
                    }
                    case 7: name = "onActivityResized"; args.putInt("height", data.readInt()); args.putInt("width", data.readInt()); args.putBundle("extras", readBundle(data)); break;
                    case 8: name = "onWarmupCompleted"; args.putBundle("extras", readBundle(data)); break;
                    case 9:
                        name = "onActivityLayout";
                        args.putInt("left", data.readInt());
                        args.putInt("top", data.readInt());
                        args.putInt("right", data.readInt());
                        args.putInt("bottom", data.readInt());
                        args.putInt("state", data.readInt());
                        args.putBundle("extras", readBundle(data));
                        break;
                    case 10: name = "onMinimized"; args.putBundle("extras", readBundle(data)); break;
                    default: name = "onUnminimized"; args.putBundle("extras", readBundle(data)); break;
                }
                if (args.getBundle("extras") == null) args.putBundle("extras", new Bundle());
                callback(session, name, arrays(args));
                // Two-way methods answer (onMessageChannelReady, onPostMessage).
                if ((flags & FLAG_ONEWAY) == 0) reply.writeNoException();
                return true;
            }
        };
    }

    /** IAuthTabCallback.Stub. */
    private static Binder authCallbackStub(int session) {
        return new Binder() {
            @Override public String getInterfaceDescriptor() { return AUTH_CALLBACK; }
            @Override protected boolean onTransact(int code, Parcel data, Parcel reply, int flags) throws RemoteException {
                if (code == INTERFACE_TRANSACTION) { reply.writeString(AUTH_CALLBACK); return true; }
                if (code == GET_INTERFACE_VERSION) { data.enforceInterface(AUTH_CALLBACK); reply.writeNoException(); reply.writeInt(1); return true; }
                if (code == GET_INTERFACE_HASH) { data.enforceInterface(AUTH_CALLBACK); reply.writeNoException(); reply.writeString("notfrozen"); return true; }
                if (code < FIRST + 1 || code > FIRST + 4) return super.onTransact(code, data, reply, flags);
                data.enforceInterface(AUTH_CALLBACK);
                Bundle args = new Bundle();
                String name;
                switch (code - FIRST) {
                    case 1: name = "onNavigationEvent"; args.putInt("event", data.readInt()); break;
                    case 2: name = "extraCallback"; args.putString("name", data.readString()); break;
                    case 3: {
                        String callbackName = data.readString();
                        Bundle callArgs = new Bundle();
                        Bundle in = readBundle(data);
                        callArgs.putBundle("extras", in != null ? in : new Bundle());
                        Object[] out = callbackWithResult(session, callbackName, arrays(callArgs));
                        reply.writeNoException();
                        writeBundle(reply, out == null ? new Bundle() : KilnBundles.toBundle(out));
                        return true;
                    }
                    default: name = "onWarmupCompleted"; break;
                }
                Bundle extras = readBundle(data);
                args.putBundle("extras", extras != null ? extras : new Bundle());
                callback(session, name, arrays(args));
                return true;
            }
        };
    }

    /** IEngagementSignalsCallback.Stub. */
    private static Binder engagementStub(int id) {
        return new Binder() {
            @Override public String getInterfaceDescriptor() { return ENGAGEMENT; }
            @Override protected boolean onTransact(int code, Parcel data, Parcel reply, int flags) throws RemoteException {
                if (code == INTERFACE_TRANSACTION) { reply.writeString(ENGAGEMENT); return true; }
                if (code == GET_INTERFACE_VERSION) { data.enforceInterface(ENGAGEMENT); reply.writeNoException(); reply.writeInt(1); return true; }
                if (code == GET_INTERFACE_HASH) { data.enforceInterface(ENGAGEMENT); reply.writeNoException(); reply.writeString("notfrozen"); return true; }
                if (code < FIRST + 1 || code > FIRST + 3) return super.onTransact(code, data, reply, flags);
                data.enforceInterface(ENGAGEMENT);
                Bundle args = new Bundle();
                String name;
                switch (code - FIRST) {
                    case 1: name = "onVerticalScrollEvent"; args.putBoolean("value", data.readInt() != 0); break;
                    case 2: name = "onGreatestScrollPercentageIncreased"; args.putInt("value", data.readInt()); break;
                    default: name = "onSessionEnded"; args.putBoolean("value", data.readInt() != 0); break;
                }
                Bundle extras = readBundle(data);
                args.putBundle("extras", extras != null ? extras : new Bundle());
                engagement(id, name, arrays(args));
                return true;
            }
        };
    }
}
