package dev.kiln;

import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.content.IntentFilter;
import android.net.ConnectivityManager;
import android.net.NetworkInfo;
import android.os.Handler;
import android.os.HandlerThread;
import android.telephony.TelephonyCallback;
import android.telephony.TelephonyDisplayInfo;
import android.telephony.TelephonyManager;

import java.util.Locale;
import java.util.concurrent.Executor;

/**
 * media3's NetworkTypeObserver and Util.getCountryCode for kiln's stream::NetworkTypeObserver
 * (adaptive streaming's initial bandwidth estimate): the active network's type, with the cellular
 * generation, reported to native code as C.NETWORK_TYPE_* on each CONNECTIVITY_ACTION.
 */
final class KilnNetworkType {
    // C.NETWORK_TYPE_*.
    private static final int UNKNOWN = 0, OFFLINE = 1, WIFI = 2, CELLULAR_2G = 3, CELLULAR_3G = 4, CELLULAR_4G = 5, CELLULAR_UNKNOWN = 6,
                             ETHERNET = 7, OTHER = 8, CELLULAR_5G_SA = 9, CELLULAR_5G_NSA = 10;

    private static Context sContext;
    private static Handler sBackground;
    private static Executor sExecutor;

    private KilnNetworkType() {}

    static native void networkTypeChanged(int networkType);

    static synchronized void attach(Context context) {
        if (sContext != null) return;
        sContext = context.getApplicationContext();
        HandlerThread thread = new HandlerThread("KilnNetworkType");
        thread.start();
        sBackground = new Handler(thread.getLooper());
        sExecutor = sBackground::post;
        IntentFilter filter = new IntentFilter(ConnectivityManager.CONNECTIVITY_ACTION);
        // A protected system broadcast: no export flag is needed.
        sContext.registerReceiver(new BroadcastReceiver() {
            @Override public void onReceive(Context c, Intent intent) { sBackground.post(KilnNetworkType::handleConnectivityAction); }
        }, filter, null, sBackground);
    }

    /** Util.getCountryCode: the network's country, else the locale's, upper case. */
    static String countryCode() {
        Context context = sContext;
        if (context != null) {
            TelephonyManager telephonyManager = (TelephonyManager) context.getSystemService(Context.TELEPHONY_SERVICE);
            if (telephonyManager != null) {
                String countryCode = telephonyManager.getNetworkCountryIso();
                if (countryCode != null && !countryCode.isEmpty()) return countryCode.toUpperCase(Locale.ROOT);
            }
        }
        return Locale.getDefault().getCountry().toUpperCase(Locale.ROOT);
    }

    private static void handleConnectivityAction() {
        int networkType = getNetworkTypeFromConnectivityManager(sContext);
        if (networkType == CELLULAR_4G) {
            disambiguate4gAnd5gNsa(sContext);
        } else {
            networkTypeChanged(networkType);
        }
    }

    @SuppressWarnings("deprecation") // ConnectivityManager.getActiveNetworkInfo, NetworkInfo: upstream's source of the subtype
    private static int getNetworkTypeFromConnectivityManager(Context context) {
        ConnectivityManager connectivityManager = (ConnectivityManager) context.getSystemService(Context.CONNECTIVITY_SERVICE);
        if (connectivityManager == null) return UNKNOWN;
        NetworkInfo networkInfo;
        try {
            networkInfo = connectivityManager.getActiveNetworkInfo();
        } catch (SecurityException e) {
            return UNKNOWN;   // no ACCESS_NETWORK_STATE
        }
        if (networkInfo == null || !networkInfo.isConnected()) return OFFLINE;
        switch (networkInfo.getType()) {
            case ConnectivityManager.TYPE_WIFI: return WIFI;
            case ConnectivityManager.TYPE_WIMAX: return CELLULAR_4G;
            case ConnectivityManager.TYPE_MOBILE:
            case ConnectivityManager.TYPE_MOBILE_DUN:
            case ConnectivityManager.TYPE_MOBILE_HIPRI: return getMobileNetworkType(networkInfo);
            case ConnectivityManager.TYPE_ETHERNET: return ETHERNET;
            default: return OTHER;
        }
    }

    @SuppressWarnings("deprecation")
    private static int getMobileNetworkType(NetworkInfo networkInfo) {
        switch (networkInfo.getSubtype()) {
            case TelephonyManager.NETWORK_TYPE_EDGE:
            case TelephonyManager.NETWORK_TYPE_GPRS:
                return CELLULAR_2G;
            case TelephonyManager.NETWORK_TYPE_1xRTT:
            case TelephonyManager.NETWORK_TYPE_CDMA:
            case TelephonyManager.NETWORK_TYPE_EVDO_0:
            case TelephonyManager.NETWORK_TYPE_EVDO_A:
            case TelephonyManager.NETWORK_TYPE_EVDO_B:
            case TelephonyManager.NETWORK_TYPE_HSDPA:
            case TelephonyManager.NETWORK_TYPE_HSPA:
            case TelephonyManager.NETWORK_TYPE_HSUPA:
            case TelephonyManager.NETWORK_TYPE_IDEN:
            case TelephonyManager.NETWORK_TYPE_UMTS:
            case TelephonyManager.NETWORK_TYPE_EHRPD:
            case TelephonyManager.NETWORK_TYPE_HSPAP:
            case TelephonyManager.NETWORK_TYPE_TD_SCDMA:
                return CELLULAR_3G;
            case TelephonyManager.NETWORK_TYPE_LTE:
                return CELLULAR_4G;
            case TelephonyManager.NETWORK_TYPE_NR:
                return CELLULAR_5G_SA;
            case TelephonyManager.NETWORK_TYPE_IWLAN:
                return WIFI;
            default:
                return CELLULAR_UNKNOWN;
        }
    }

    /** TelephonyDisplayInfo tells 5G NSA (reported as LTE) from 4G. */
    private static void disambiguate4gAnd5gNsa(Context context) {
        try {
            TelephonyManager telephonyManager = (TelephonyManager) context.getSystemService(Context.TELEPHONY_SERVICE);
            DisplayInfoCallback callback = new DisplayInfoCallback();
            telephonyManager.registerTelephonyCallback(sExecutor, callback);
            telephonyManager.unregisterTelephonyCallback(callback);
        } catch (RuntimeException e) {
            networkTypeChanged(CELLULAR_4G);
        }
    }

    private static final class DisplayInfoCallback extends TelephonyCallback implements TelephonyCallback.DisplayInfoListener {
        @Override
        public void onDisplayInfoChanged(TelephonyDisplayInfo telephonyDisplayInfo) {
            int overrideNetworkType = telephonyDisplayInfo.getOverrideNetworkType();
            boolean is5gNsa = overrideNetworkType == TelephonyDisplayInfo.OVERRIDE_NETWORK_TYPE_NR_NSA
                    || overrideNetworkType == TelephonyDisplayInfo.OVERRIDE_NETWORK_TYPE_NR_NSA_MMWAVE
                    || overrideNetworkType == TelephonyDisplayInfo.OVERRIDE_NETWORK_TYPE_NR_ADVANCED;
            networkTypeChanged(is5gNsa ? CELLULAR_5G_NSA : CELLULAR_4G);
        }
    }
}
