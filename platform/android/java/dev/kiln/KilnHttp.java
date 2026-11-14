package dev.kiln;

import android.Manifest;
import android.content.Context;
import android.content.pm.PackageManager;
import android.net.ConnectivityManager;
import android.net.Network;
import android.net.NetworkCapabilities;
import android.os.Handler;
import android.os.Looper;

import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.net.ConnectException;
import java.net.HttpURLConnection;
import java.net.MalformedURLException;
import java.net.NoRouteToHostException;
import java.net.SocketTimeoutException;
import java.net.URL;
import java.net.UnknownHostException;
import java.util.ArrayList;
import java.util.List;
import java.util.Map;
import java.util.concurrent.ConcurrentHashMap;

import javax.net.ssl.SSLException;

/**
 * kiln's net::Fetch on Android: HttpURLConnection, so requests use the platform's TLS and trust
 * store, proxies and the app's network security config. Called on kiln's IO threads. Also the
 * default network's state for net::Connectivity.
 */
final class KilnHttp {
    private static final int MAX_REDIRECTS = 20;   // as OkHttp's RetryAndFollowUpInterceptor
    private static final ConcurrentHashMap<Long, HttpURLConnection> sCalls = new ConcurrentHashMap<>();
    private static Context sContext;
    private static boolean sWatching;

    private KilnHttp() {}

    /** Body bytes received so far; false when the call was cancelled. */
    private static native boolean progress(long call, long received, long total);
    private static native void networkChanged(boolean connected, boolean validated, boolean metered, int transport);

    /**
     * Runs the request. Returns { null, status, message, String[] headers (name, value, ...),
     * byte[] body, url } for a response, or { kind, message } when there is none; the kinds are
     * net::HttpError::Kind's names.
     */
    static Object[] execute(long call, String method, String url, String[] headers, byte[] body, String bodyFile,
                            String responseFile, int connectTimeoutMs, int readTimeoutMs, boolean followRedirects) {
        HttpURLConnection connection = null;
        try {
            URL target = new URL(url);
            for (int redirects = 0; ; ++redirects) {
                String protocol = target.getProtocol();
                if (!"http".equals(protocol) && !"https".equals(protocol)) return error("InvalidUrl", "unsupported scheme: " + protocol);
                connection = (HttpURLConnection) target.openConnection();
                sCalls.put(call, connection);
                connection.setConnectTimeout(connectTimeoutMs);
                connection.setReadTimeout(readTimeoutMs);
                // Redirects are followed here: HttpURLConnection does not cross http and https.
                connection.setInstanceFollowRedirects(false);
                connection.setRequestMethod(method);
                connection.setUseCaches(false);
                for (int i = 0; i + 1 < headers.length; i += 2) connection.addRequestProperty(headers[i], headers[i + 1]);
                boolean hasBody = body != null || bodyFile != null;
                if (hasBody) {
                    connection.setDoOutput(true);
                    if (body != null) {
                        connection.setFixedLengthStreamingMode(body.length);
                        try (OutputStream out = connection.getOutputStream()) { out.write(body); }
                    } else {
                        File file = new File(bodyFile);
                        connection.setFixedLengthStreamingMode(file.length());
                        try (InputStream in = new FileInputStream(file); OutputStream out = connection.getOutputStream()) {
                            byte[] buffer = new byte[16384];
                            for (int n; (n = in.read(buffer)) > 0; ) out.write(buffer, 0, n);
                        }
                    }
                }
                int status = connection.getResponseCode();
                String location = connection.getHeaderField("Location");
                boolean redirect = status == 301 || status == 302 || status == 303 || status == 307 || status == 308;
                if (followRedirects && redirect && location != null) {
                    if (redirects >= MAX_REDIRECTS) return error("TooManyRedirects", "too many redirects: " + (redirects + 1));
                    target = new URL(target, location);
                    // 303, and 301 / 302 for anything but GET and HEAD, go on as a GET without the body.
                    boolean keeps = status == 307 || status == 308 || "GET".equals(method) || "HEAD".equals(method);
                    if (!keeps) {
                        method = "GET";
                        body = null;
                        bodyFile = null;
                        headers = withoutBodyHeaders(headers);
                    }
                    drain(connection);
                    connection.disconnect();
                    continue;
                }

                List<String> fields = new ArrayList<>();
                for (Map.Entry<String, List<String>> e : connection.getHeaderFields().entrySet()) {
                    if (e.getKey() == null) continue;   // the status line
                    for (String value : e.getValue()) { fields.add(e.getKey()); fields.add(value); }
                }
                long total = connection.getContentLengthLong();
                byte[] bytes = null;
                InputStream in = status >= 400 ? connection.getErrorStream() : connection.getInputStream();
                if (in != null) {
                    try (InputStream stream = in;
                         OutputStream out = responseFile != null ? new FileOutputStream(responseFile) : new ByteArrayOutputStream()) {
                        byte[] buffer = new byte[16384];
                        long received = 0;
                        for (int n; (n = stream.read(buffer)) > 0; ) {
                            out.write(buffer, 0, n);
                            received += n;
                            if (!progress(call, received, total)) return error("Cancelled", "cancelled");
                        }
                        if (responseFile == null) bytes = ((ByteArrayOutputStream) out).toByteArray();
                    }
                }
                String message = connection.getResponseMessage();
                return new Object[] { null, status, message == null ? "" : message, fields.toArray(new String[0]),
                                      bytes == null ? new byte[0] : bytes, connection.getURL().toString() };
            }
        } catch (MalformedURLException e) {
            return error("InvalidUrl", text(e));
        } catch (UnknownHostException e) {
            return error("UnknownHost", text(e));
        } catch (SocketTimeoutException e) {
            return error("Timeout", text(e));
        } catch (SSLException e) {
            return error("Tls", text(e));
        } catch (ConnectException | NoRouteToHostException e) {
            return error("Connect", text(e));
        } catch (SecurityException e) {
            return error("Permission", text(e));
        } catch (IOException e) {
            // NetworkSecurityPolicy: "Cleartext HTTP traffic to <host> not permitted".
            String message = text(e);
            return error(message.contains("Cleartext HTTP traffic") || message.contains("CLEARTEXT communication") ? "Cleartext" : "Io", message);
        } catch (RuntimeException e) {
            return error("Io", text(e));
        } finally {
            sCalls.remove(call);
            if (connection != null) connection.disconnect();
        }
    }

    /** Interrupts the call's connection (from any thread). */
    static void cancel(long call) {
        HttpURLConnection connection = sCalls.get(call);
        if (connection != null) {
            // Closing a socket is network work to StrictMode: not on the caller's (main) thread.
            new Thread(connection::disconnect, "kiln-http-cancel").start();
        }
    }

    private static Object[] error(String kind, String message) { return new Object[] { kind, message }; }

    private static String text(Throwable e) {
        String message = e.getMessage();
        return message == null || message.isEmpty() ? e.getClass().getSimpleName() : message;
    }

    private static String[] withoutBodyHeaders(String[] headers) {
        List<String> kept = new ArrayList<>();
        for (int i = 0; i + 1 < headers.length; i += 2) {
            String name = headers[i];
            if (name.equalsIgnoreCase("Content-Type") || name.equalsIgnoreCase("Content-Length") || name.equalsIgnoreCase("Transfer-Encoding")) continue;
            kept.add(name);
            kept.add(headers[i + 1]);
        }
        return kept.toArray(new String[0]);
    }

    private static void drain(HttpURLConnection connection) {
        try (InputStream in = connection.getResponseCode() >= 400 ? connection.getErrorStream() : connection.getInputStream()) {
            if (in == null) return;
            byte[] buffer = new byte[4096];
            while (in.read(buffer) > 0) { /* the connection goes back to the pool */ }
        } catch (IOException ignored) {
        }
    }

    /** Watches the default network when the app holds ACCESS_NETWORK_STATE. */
    static void attach(Context context) {
        sContext = context.getApplicationContext();
        if (sWatching) return;
        if (sContext.checkSelfPermission(Manifest.permission.ACCESS_NETWORK_STATE) != PackageManager.PERMISSION_GRANTED) return;
        ConnectivityManager manager = sContext.getSystemService(ConnectivityManager.class);
        if (manager == null) return;
        sWatching = true;
        Handler main = new Handler(Looper.getMainLooper());
        Network active = manager.getActiveNetwork();
        NetworkCapabilities now = active == null ? null : manager.getNetworkCapabilities(active);
        report(now);
        manager.registerDefaultNetworkCallback(new ConnectivityManager.NetworkCallback() {
            @Override public void onCapabilitiesChanged(Network network, NetworkCapabilities capabilities) { report(capabilities); }
            @Override public void onLost(Network network) { report(null); }
        }, main);
    }

    private static void report(NetworkCapabilities c) {
        if (c == null) {
            networkChanged(false, false, false, 0);
            return;
        }
        // NetworkState::Transport's order.
        int transport = 0;
        if (c.hasTransport(NetworkCapabilities.TRANSPORT_VPN)) transport = 5;
        else if (c.hasTransport(NetworkCapabilities.TRANSPORT_WIFI)) transport = 1;
        else if (c.hasTransport(NetworkCapabilities.TRANSPORT_CELLULAR)) transport = 2;
        else if (c.hasTransport(NetworkCapabilities.TRANSPORT_ETHERNET)) transport = 3;
        else if (c.hasTransport(NetworkCapabilities.TRANSPORT_BLUETOOTH)) transport = 4;
        networkChanged(c.hasCapability(NetworkCapabilities.NET_CAPABILITY_INTERNET),
                       c.hasCapability(NetworkCapabilities.NET_CAPABILITY_VALIDATED),
                       !c.hasCapability(NetworkCapabilities.NET_CAPABILITY_NOT_METERED), transport);
    }
}
