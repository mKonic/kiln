package dev.kiln;

import android.app.Activity;
import android.content.Context;
import android.os.Build;
import android.os.CancellationSignal;
import android.os.OutcomeReceiver;
import android.view.View;

import java.util.HashMap;

/**
 * ui::GetCredential / CreateCredential / ClearCredentialState over
 * android.credentials.CredentialManager (API 34+), as androidx.credentials'
 * CredentialProviderFrameworkImpl calls it. Main thread only; each call answers once through
 * {@link #result}.
 */
final class KilnCredentials {
    private KilnCredentials() {}

    private static final HashMap<Integer, CancellationSignal> sSignals = new HashMap<>();
    private static final String UNSUPPORTED = "Your device doesn't support credential manager";

    static native void result(int id, boolean ok, String type, Object[] data, String message);

    private static Context contextOf(View view) {
        Activity a = KilnPip.activityOf(view);
        return a != null ? a : view.getContext();
    }

    @android.annotation.TargetApi(34)
    private static android.credentials.CredentialManager manager(Context c) {
        if (Build.VERSION.SDK_INT < 34) return null;
        return (android.credentials.CredentialManager) c.getSystemService(Context.CREDENTIAL_SERVICE);
    }

    private static CancellationSignal signal(int id) {
        CancellationSignal s = new CancellationSignal();
        sSignals.put(id, s);
        return s;
    }

    private static final android.os.Handler sMain = new android.os.Handler(android.os.Looper.getMainLooper());

    // Native is answered on the main thread. clearCredentialState's transport calls back on a
    // binder thread whatever the executor (CredentialManager.ClearCredentialStateTransport).
    private static void finish(int id, boolean ok, String type, Object[] data, String message) {
        if (android.os.Looper.myLooper() != android.os.Looper.getMainLooper()) {
            sMain.post(() -> finish(id, ok, type, data, message));
            return;
        }
        sSignals.remove(id);
        result(id, ok, type, data, message);
    }

    /**
     * request: {metadata bundle, Object[] options, origin or null}; an option is {type, request
     * bundle, candidate query bundle, "true" / "false" for isSystemProviderRequired}. False when
     * unsupported (answered already).
     */
    @android.annotation.TargetApi(34)
    static boolean get(View view, int id, Object[] request) {
        Context c = contextOf(view);
        android.credentials.CredentialManager cm = manager(c);
        if (cm == null) {
            result(id, false, "androidx.credentials.TYPE_GET_CREDENTIAL_UNSUPPORTED_EXCEPTION", null, UNSUPPORTED);
            return false;
        }
        try {
            android.credentials.GetCredentialRequest.Builder b =
                    new android.credentials.GetCredentialRequest.Builder(KilnBundles.toBundle((Object[]) request[0]));
            for (Object o : (Object[]) request[1]) {
                Object[] opt = (Object[]) o;
                b.addCredentialOption(new android.credentials.CredentialOption.Builder(
                        (String) opt[0], KilnBundles.toBundle((Object[]) opt[1]), KilnBundles.toBundle((Object[]) opt[2]))
                        .setIsSystemProviderRequired("true".equals(opt[3])).build());
            }
            if (request[2] != null) b.setOrigin((String) request[2]);
            cm.getCredential(c, b.build(), signal(id), c.getMainExecutor(),
                    new OutcomeReceiver<android.credentials.GetCredentialResponse, android.credentials.GetCredentialException>() {
                        @Override public void onResult(android.credentials.GetCredentialResponse response) {
                            android.credentials.Credential cr = response.getCredential();
                            finish(id, true, cr.getType(), KilnBundles.fromBundle(cr.getData()), null);
                        }
                        @Override public void onError(android.credentials.GetCredentialException e) {
                            finish(id, false, e.getType(), null, e.getMessage());
                        }
                    });
        } catch (RuntimeException e) {   // a bad request, or SecurityException for an origin
            finish(id, false, "android.credentials.GetCredentialException.TYPE_UNKNOWN", null, String.valueOf(e.getMessage()));
        }
        return true;
    }

    /** CreateCredentialRequest(type, final credential data, candidate query data) with its flags. */
    @android.annotation.TargetApi(34)
    static boolean create(View view, int id, String type, Object[] data, Object[] query, boolean systemProviderRequired, String origin) {
        Context c = contextOf(view);
        android.credentials.CredentialManager cm = manager(c);
        if (cm == null) {
            result(id, false, "androidx.credentials.TYPE_CREATE_CREDENTIAL_UNSUPPORTED_EXCEPTION", null, UNSUPPORTED);
            return false;
        }
        try {
            android.credentials.CreateCredentialRequest.Builder b = new android.credentials.CreateCredentialRequest.Builder(
                    type, KilnBundles.toBundle(data), KilnBundles.toBundle(query))
                    .setIsSystemProviderRequired(systemProviderRequired)
                    .setAlwaysSendAppInfoToProvider(true);
            if (origin != null) b.setOrigin(origin);
            cm.createCredential(c, b.build(), signal(id), c.getMainExecutor(),
                    new OutcomeReceiver<android.credentials.CreateCredentialResponse, android.credentials.CreateCredentialException>() {
                        @Override public void onResult(android.credentials.CreateCredentialResponse response) {
                            finish(id, true, type, KilnBundles.fromBundle(response.getData()), null);
                        }
                        @Override public void onError(android.credentials.CreateCredentialException e) {
                            finish(id, false, e.getType(), null, e.getMessage());
                        }
                    });
        } catch (RuntimeException e) {
            finish(id, false, "android.credentials.CreateCredentialException.TYPE_UNKNOWN", null, String.valueOf(e.getMessage()));
        }
        return true;
    }

    @android.annotation.TargetApi(34)
    static boolean clear(View view, int id) {
        Context c = contextOf(view);
        android.credentials.CredentialManager cm = manager(c);
        if (cm == null) {
            result(id, false, "androidx.credentials.TYPE_CLEAR_CREDENTIAL_UNSUPPORTED_EXCEPTION", null, UNSUPPORTED);
            return false;
        }
        cm.clearCredentialState(new android.credentials.ClearCredentialStateRequest(new android.os.Bundle()), signal(id), c.getMainExecutor(),
                new OutcomeReceiver<Void, android.credentials.ClearCredentialStateException>() {
                    @Override public void onResult(Void v) { finish(id, true, "", null, null); }
                    // androidx answers every framework error as ClearCredentialUnknownException.
                    @Override public void onError(android.credentials.ClearCredentialStateException e) {
                        finish(id, false, "android.credentials.ClearCredentialStateException.TYPE_UNKNOWN", null, null);
                    }
                });
        return true;
    }

    static void cancel(int id) {
        CancellationSignal s = sSignals.remove(id);
        if (s != null) s.cancel();
    }
}
