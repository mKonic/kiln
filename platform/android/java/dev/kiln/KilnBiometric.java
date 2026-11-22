package dev.kiln;

import android.content.Context;
import android.graphics.Bitmap;
import android.graphics.BitmapFactory;
import android.hardware.biometrics.BiometricManager;
import android.hardware.biometrics.BiometricPrompt;
import android.hardware.biometrics.PromptContentItemBulletedText;
import android.hardware.biometrics.PromptContentItemPlainText;
import android.hardware.biometrics.PromptContentViewWithMoreOptionsButton;
import android.hardware.biometrics.PromptVerticalListContentView;
import android.os.Build;
import android.os.CancellationSignal;
import android.util.Log;

import dev.kiln.bridge.Codec;

import java.security.KeyFactory;
import java.security.KeyStore;
import java.security.PrivateKey;
import java.security.PublicKey;
import java.security.Signature;
import java.security.spec.X509EncodedKeySpec;
import java.util.HashMap;
import java.util.concurrent.Executor;

import javax.crypto.Cipher;
import javax.crypto.KeyAgreement;
import javax.crypto.Mac;
import javax.crypto.SecretKey;
import javax.crypto.spec.GCMParameterSpec;
import javax.crypto.spec.IvParameterSpec;

/**
 * ui::Biometric's prompt (androidx.biometric's AuthenticationHandlerBiometricPrompt): the
 * request comes encoded from native code; events go back as they happen. A CryptoObject's
 * Keystore operation is set up before the prompt and run once it unlocks.
 */
final class KilnBiometric {
    private static final HashMap<Integer, CancellationSignal> sPrompts = new HashMap<>();
    private static final int EVENT_SUCCEEDED = 0, EVENT_ERROR = 1, EVENT_FAILED = 2, EVENT_FALLBACK = 3;
    private static final int NEGATIVE_ERROR = 0, NEGATIVE_CANCEL = 1, NEGATIVE_FALLBACK = 2;
    private static final int CRYPTO_CIPHER = 0, CRYPTO_SIGNATURE = 1, CRYPTO_MAC = 2, CRYPTO_KEY_AGREEMENT = 3, CRYPTO_OPERATION = 4;
    private static final int ERROR_CRYPTO_UNAVAILABLE = -100;

    private KilnBiometric() {}

    static native void event(int id, byte[] event);

    private static Context context() { return KilnContent.context(); }

    static int canAuthenticate(int authenticators) {
        Context c = context();
        BiometricManager bm = c == null ? null : c.getSystemService(BiometricManager.class);
        return bm == null ? 12 : bm.canAuthenticate(authenticators);
    }

    /** BiometricManager.Strings: buttonLabel, promptMessage, settingName (null each when none); null without a manager. */
    static String[] strings(int authenticators) {
        Context c = context();
        BiometricManager bm = c == null ? null : c.getSystemService(BiometricManager.class);
        if (bm == null) return null;
        try {
            BiometricManager.Strings s = bm.getStrings(authenticators);
            CharSequence b = s.getButtonLabel(), p = s.getPromptMessage(), n = s.getSettingName();
            return new String[] { b == null ? null : b.toString(), p == null ? null : p.toString(), n == null ? null : n.toString() };
        } catch (SecurityException | IllegalArgumentException e) {
            return null;
        }
    }

    /** multipleFallbackOptionsValid's API check: API 36.1. */
    static boolean multipleFallbackOptions() {
        return Build.VERSION.SDK_INT >= 36 && Build.VERSION.SDK_INT_FULL >= Build.VERSION_CODES_FULL.BAKLAVA_1;
    }

    private static void send(int id, int kind, int code, String message, int authType, int fallback, byte[] output, byte[] outputIv) {
        Codec.Writer w = new Codec.Writer();
        w.writeInt(kind);
        w.writeInt(code);
        w.writeString(message == null ? "" : message);
        w.writeInt(authType);
        w.writeInt(fallback);
        w.writeBoolean(output != null);
        w.writeBytes(output == null ? new byte[0] : output);
        w.writeBytes(outputIv == null ? new byte[0] : outputIv);
        event(id, w.toByteArray());
    }
    private static void finish(int id, int kind, int code, String message) {
        sPrompts.remove(id);
        send(id, kind, code, message, 0, 0, null, null);
    }

    /** A Keystore operation, set up before the prompt and finished once it unlocks. */
    private static final class Crypto {
        int kind, mode;
        String alias, algorithm;
        byte[] iv, aad, input, peer;
        long handle;
        Cipher cipher;
        Signature signature;
        Mac mac;
        KeyAgreement agreement;
        BiometricPrompt.CryptoObject object;

        void init() throws Exception {
            KeyStore ks = KeyStore.getInstance("AndroidKeyStore");
            ks.load(null);
            switch (kind) {
                case CRYPTO_CIPHER: {
                    cipher = Cipher.getInstance(algorithm);
                    SecretKey key = (SecretKey) ks.getKey(alias, null);
                    if (key == null) throw new IllegalStateException("no key " + alias);
                    if (mode == 0) cipher.init(Cipher.ENCRYPT_MODE, key);
                    else if (algorithm.contains("GCM")) cipher.init(Cipher.DECRYPT_MODE, key, new GCMParameterSpec(128, iv));
                    else cipher.init(Cipher.DECRYPT_MODE, key, new IvParameterSpec(iv));
                    object = new BiometricPrompt.CryptoObject(cipher);
                    break;
                }
                case CRYPTO_SIGNATURE: {
                    signature = Signature.getInstance(algorithm);
                    PrivateKey key = (PrivateKey) ks.getKey(alias, null);
                    if (key == null) throw new IllegalStateException("no key " + alias);
                    signature.initSign(key);
                    object = new BiometricPrompt.CryptoObject(signature);
                    break;
                }
                case CRYPTO_MAC: {
                    mac = Mac.getInstance(algorithm);
                    SecretKey key = (SecretKey) ks.getKey(alias, null);
                    if (key == null) throw new IllegalStateException("no key " + alias);
                    mac.init(key);
                    object = new BiometricPrompt.CryptoObject(mac);
                    break;
                }
                case CRYPTO_KEY_AGREEMENT: {
                    if (Build.VERSION.SDK_INT < 36 || Build.VERSION.SDK_INT_FULL < Build.VERSION_CODES_FULL.BAKLAVA_1)
                        throw new UnsupportedOperationException("a KeyAgreement CryptoObject needs API 36.1");
                    agreement = KeyAgreement.getInstance(algorithm);
                    PrivateKey key = (PrivateKey) ks.getKey(alias, null);
                    if (key == null) throw new IllegalStateException("no key " + alias);
                    agreement.init(key);
                    object = new BiometricPrompt.CryptoObject(agreement);
                    break;
                }
                default: {
                    if (Build.VERSION.SDK_INT < 35) throw new UnsupportedOperationException("an operation handle CryptoObject needs API 35");
                    object = new BiometricPrompt.CryptoObject(handle);
                    break;
                }
            }
        }

        /** The unlocked operation's output (and the IV encryption chose). */
        byte[][] run() throws Exception {
            switch (kind) {
                case CRYPTO_CIPHER:
                    if (aad.length > 0) cipher.updateAAD(aad);
                    byte[] out = cipher.doFinal(input);
                    return new byte[][] { out, mode == 0 ? cipher.getIV() : null };
                case CRYPTO_SIGNATURE:
                    signature.update(input);
                    return new byte[][] { signature.sign(), null };
                case CRYPTO_MAC:
                    return new byte[][] { mac.doFinal(input), null };
                case CRYPTO_KEY_AGREEMENT: {
                    String keyAlgorithm = algorithm.startsWith("X") ? "XDH" : "EC";
                    PublicKey peerKey = KeyFactory.getInstance(keyAlgorithm).generatePublic(new X509EncodedKeySpec(peer));
                    agreement.doPhase(peerKey, true);
                    return new byte[][] { agreement.generateSecret(), null };
                }
                default:
                    return new byte[][] { new byte[0], null };
            }
        }
    }

    static boolean authenticate(int id, byte[] request) {
        Context c = context();
        if (c == null) return false;
        Codec.Reader r = new Codec.Reader(request);
        String title = r.readString(), subtitle = r.readString(), description = r.readString(), negative = r.readString();
        int negativeAction = r.readInt(), allowed = r.readInt();
        boolean confirm = r.readBoolean();
        String logoRes = r.readString();
        byte[] logoBitmap = r.readBytes();
        String logoDescription = r.readString();
        boolean hasContent = r.readBoolean();
        int contentKind = r.readInt();
        String contentDescription = r.readString();
        int itemCount = r.readInt();
        int[] itemKinds = new int[itemCount];
        String[] itemTexts = new String[itemCount];
        for (int i = 0; i < itemCount; i++) { itemKinds[i] = r.readInt(); itemTexts[i] = r.readString(); }
        int fallbackCount = r.readInt();
        String[] fallbackTexts = new String[fallbackCount];
        int[] fallbackIcons = new int[fallbackCount];
        for (int i = 0; i < fallbackCount; i++) { fallbackTexts[i] = r.readString(); fallbackIcons[i] = r.readInt(); }
        Crypto crypto = null;
        if (r.readBoolean()) {
            crypto = new Crypto();
            crypto.kind = r.readInt();
            crypto.alias = r.readString();
            crypto.algorithm = r.readString();
            crypto.mode = r.readInt();
            crypto.iv = r.readBytes();
            crypto.aad = r.readBytes();
            crypto.input = r.readBytes();
            crypto.peer = r.readBytes();
            crypto.handle = r.readLong();
        }
        Executor main = c.getMainExecutor();
        try {
            BiometricPrompt.Builder b = new BiometricPrompt.Builder(c).setTitle(title).setAllowedAuthenticators(allowed)
                    .setConfirmationRequired(confirm);
            if (!subtitle.isEmpty()) b.setSubtitle(subtitle);
            if (!description.isEmpty()) b.setDescription(description);
            // The negative button: refused alongside DEVICE_CREDENTIAL (the PIN is the way out).
            if ((allowed & BiometricManager.Authenticators.DEVICE_CREDENTIAL) == 0) {
                String text = !negative.isEmpty() ? negative : negativeAction == NEGATIVE_CANCEL ? c.getString(android.R.string.cancel) : "";
                if (!text.isEmpty()) {
                    final String shown = text;
                    b.setNegativeButton(text, main, (d, w) -> {
                        if (!sPrompts.containsKey(id)) return;
                        switch (negativeAction) {
                            case NEGATIVE_CANCEL:
                                finish(id, EVENT_ERROR, 5 /* ERROR_CANCELED */, KilnBiometricStrings.pick(KilnBiometricStrings.USER_CANCELED, c.getResources().getConfiguration().getLocales()));
                                break;
                            case NEGATIVE_FALLBACK:
                                sPrompts.remove(id);
                                send(id, EVENT_FALLBACK, 0, shown, 0, 0, null, null);
                                break;
                            default:
                                finish(id, EVENT_ERROR, 13 /* ERROR_NEGATIVE_BUTTON */, shown);
                                break;
                        }
                    });
                }
            }
            // Several fallbacks (API 36.1): the system lists them.
            if (fallbackCount > 1 && multipleFallbackOptions()) {
                for (int i = 0; i < fallbackCount; i++) {
                    final int index = i;
                    b.addFallbackOption(fallbackTexts[i], fallbackIcons[i], main, (d, w) -> {
                        if (!sPrompts.containsKey(id)) return;
                        sPrompts.remove(id);
                        send(id, EVENT_FALLBACK, 0, fallbackTexts[index], 0, index, null, null);
                    });
                }
            }
            if (Build.VERSION.SDK_INT >= 35) {
                try {
                    if (!logoRes.isEmpty()) b.setLogoRes(c.getResources().getIdentifier(logoRes, "drawable", c.getPackageName()));
                    else if (logoBitmap.length > 0) {
                        Bitmap bm = BitmapFactory.decodeByteArray(logoBitmap, 0, logoBitmap.length);
                        if (bm != null) b.setLogoBitmap(bm);
                    }
                    if (!logoDescription.isEmpty()) b.setLogoDescription(logoDescription);
                } catch (SecurityException e) {
                    Log.w("kiln", "biometric prompt: a logo needs SET_BIOMETRIC_DIALOG_ADVANCED");
                }
                if (hasContent) {
                    if (contentKind == 0) {
                        PromptVerticalListContentView.Builder v = new PromptVerticalListContentView.Builder();
                        if (!contentDescription.isEmpty()) v.setDescription(contentDescription);
                        for (int i = 0; i < itemCount; i++)
                            v.addListItem(itemKinds[i] == 0 ? new PromptContentItemPlainText(itemTexts[i]) : new PromptContentItemBulletedText(itemTexts[i]));
                        b.setContentView(v.build());
                    } else {
                        PromptContentViewWithMoreOptionsButton.Builder v = new PromptContentViewWithMoreOptionsButton.Builder();
                        if (!contentDescription.isEmpty()) v.setDescription(contentDescription);
                        v.setMoreOptionsButtonListener(main, (d, w) -> {
                            if (!sPrompts.containsKey(id)) return;
                            CancellationSignal s = sPrompts.get(id);
                            finish(id, EVENT_ERROR, 22 /* ERROR_CONTENT_VIEW_MORE_OPTIONS_BUTTON */,
                                    KilnBiometricStrings.pick(KilnBiometricStrings.MORE_OPTIONS, c.getResources().getConfiguration().getLocales()));
                            if (s != null) s.cancel();
                        });
                        b.setContentView(v.build());
                    }
                }
            }
            if (crypto != null) {
                try {
                    crypto.init();
                } catch (Exception e) {
                    // Kotlin's Cipher.init would throw here (KeyPermanentlyInvalidatedException...).
                    Log.w("kiln", "biometric prompt: the crypto object's key cannot be used: " + e);
                    final String message = e.toString();
                    main.execute(() -> send(id, EVENT_ERROR, ERROR_CRYPTO_UNAVAILABLE, message, 0, 0, null, null));
                    return true;
                }
            }
            CancellationSignal signal = new CancellationSignal();
            sPrompts.put(id, signal);
            final Crypto unlocked = crypto;
            BiometricPrompt.AuthenticationCallback callback = new BiometricPrompt.AuthenticationCallback() {
                @Override public void onAuthenticationSucceeded(BiometricPrompt.AuthenticationResult result) {
                    if (sPrompts.remove(id) == null) return;
                    byte[] out = null, outIv = null;
                    if (unlocked != null) {
                        try {
                            byte[][] o = unlocked.run();
                            out = o[0];
                            outIv = o[1];
                        } catch (Exception e) {
                            send(id, EVENT_ERROR, ERROR_CRYPTO_UNAVAILABLE, e.toString(), 0, 0, null, null);
                            return;
                        }
                    }
                    send(id, EVENT_SUCCEEDED, 0, "", result.getAuthenticationType(), 0, out, outIv);
                }
                @Override public void onAuthenticationError(int code, CharSequence message) {
                    if (sPrompts.remove(id) == null) return;
                    String text = message != null ? message.toString()
                            : KilnBiometricStrings.pick(KilnBiometricStrings.DEFAULT_ERROR, c.getResources().getConfiguration().getLocales());
                    send(id, EVENT_ERROR, code, text, 0, 0, null, null);
                }
                @Override public void onAuthenticationFailed() {
                    if (sPrompts.containsKey(id)) send(id, EVENT_FAILED, 0, "", 0, 0, null, null);
                }
            };
            if (crypto != null) b.build().authenticate(crypto.object, signal, main, callback);
            else b.build().authenticate(signal, main, callback);
            return true;
        } catch (IllegalArgumentException | IllegalStateException | SecurityException e) {
            sPrompts.remove(id);
            Log.w("kiln", "biometric prompt: " + e.getMessage());
            return false;
        }
    }

    /** cancelAuthentication: the platform then reports ERROR_CANCELED. */
    static void cancel(int id) {
        CancellationSignal s = sPrompts.get(id);
        if (s != null) s.cancel();
    }
}
