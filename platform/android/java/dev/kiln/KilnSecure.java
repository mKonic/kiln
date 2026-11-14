package dev.kiln;

import android.security.keystore.KeyGenParameterSpec;
import android.security.keystore.KeyProperties;
import android.util.Log;

import java.nio.charset.StandardCharsets;
import java.security.KeyStore;
import java.util.Arrays;

import javax.crypto.Cipher;
import javax.crypto.KeyGenerator;
import javax.crypto.SecretKey;
import javax.crypto.spec.GCMParameterSpec;

/**
 * security::SecureStore's key store: AES-256-GCM with keys the Android Keystore makes and keeps
 * (never exported). The Keystore picks each nonce; it is stored before the ciphertext.
 */
final class KilnSecure {
    private static final String KEYSTORE = "AndroidKeyStore";
    private static final String TRANSFORM = "AES/GCM/NoPadding";
    private static final int NONCE = 12, TAG_BITS = 128;

    private KilnSecure() {}

    private static SecretKey key(String alias, boolean create) throws Exception {
        KeyStore ks = KeyStore.getInstance(KEYSTORE);
        ks.load(null);
        KeyStore.Entry e = ks.getEntry(alias, null);
        if (e instanceof KeyStore.SecretKeyEntry) return ((KeyStore.SecretKeyEntry) e).getSecretKey();
        if (!create) return null;
        KeyGenerator g = KeyGenerator.getInstance(KeyProperties.KEY_ALGORITHM_AES, KEYSTORE);
        g.init(new KeyGenParameterSpec.Builder(alias, KeyProperties.PURPOSE_ENCRYPT | KeyProperties.PURPOSE_DECRYPT)
                .setBlockModes(KeyProperties.BLOCK_MODE_GCM)
                .setEncryptionPaddings(KeyProperties.ENCRYPTION_PADDING_NONE)
                .setKeySize(256)
                .build());
        return g.generateKey();
    }

    /** The nonce and the ciphertext with its tag; null on failure. */
    static byte[] encrypt(String alias, byte[] plain, String associatedData) {
        try {
            Cipher c = Cipher.getInstance(TRANSFORM);
            c.init(Cipher.ENCRYPT_MODE, key(alias, true));
            c.updateAAD(associatedData.getBytes(StandardCharsets.UTF_8));
            byte[] body = c.doFinal(plain);
            byte[] iv = c.getIV();
            byte[] out = Arrays.copyOf(iv, iv.length + body.length);
            System.arraycopy(body, 0, out, iv.length, body.length);
            return out;
        } catch (Exception e) {
            Log.e("kiln", "SecureStore: cannot encrypt with " + alias, e);
            return null;
        }
    }

    /** The plaintext, or null when the key is gone or the data or its associated data changed. */
    static byte[] decrypt(String alias, byte[] sealed, String associatedData) {
        try {
            SecretKey k = key(alias, false);
            if (k == null || sealed.length < NONCE) return null;
            Cipher c = Cipher.getInstance(TRANSFORM);
            c.init(Cipher.DECRYPT_MODE, k, new GCMParameterSpec(TAG_BITS, sealed, 0, NONCE));
            c.updateAAD(associatedData.getBytes(StandardCharsets.UTF_8));
            return c.doFinal(sealed, NONCE, sealed.length - NONCE);
        } catch (Exception e) {
            Log.w("kiln", "SecureStore: cannot decrypt with " + alias + ": " + e);
            return null;
        }
    }
}
