package dev.kiln;

import android.security.keystore.KeyGenParameterSpec;
import android.security.keystore.KeyProperties;
import android.util.Log;

import dev.kiln.bridge.Codec;

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

    // --- security::GenerateKey and the rest of KeyStore.h -------------------------------------

    private static String[] strings(Codec.Reader r) {
        String[] out = new String[r.readInt()];
        for (int i = 0; i < out.length; i++) out[i] = r.readString();
        return out;
    }

    /** KeyGenParameterSpec from security::KeyGenSpec's encoding. */
    static boolean generateKey(String alias, byte[] spec) {
        try {
            Codec.Reader r = new Codec.Reader(spec);
            int algorithm = r.readInt(), keySize = r.readInt(), purposes = r.readInt();
            String[] blockModes = strings(r), paddings = strings(r), digests = strings(r), signaturePaddings = strings(r);
            boolean authRequired = r.readBoolean();
            int timeout = r.readInt(), authTypes = r.readInt();
            boolean invalidated = r.readBoolean(), unlocked = r.readBoolean(), strongBox = r.readBoolean();
            KeyGenParameterSpec.Builder b = new KeyGenParameterSpec.Builder(alias, purposes);
            if (keySize > 0) b.setKeySize(keySize);
            if (blockModes.length > 0) b.setBlockModes(blockModes);
            if (paddings.length > 0) b.setEncryptionPaddings(paddings);
            if (digests.length > 0) b.setDigests(digests);
            if (signaturePaddings.length > 0) b.setSignaturePaddings(signaturePaddings);
            if (authRequired) {
                b.setUserAuthenticationRequired(true);
                b.setUserAuthenticationParameters(timeout, authTypes);
                b.setInvalidatedByBiometricEnrollment(invalidated);
            }
            if (unlocked) b.setUnlockedDeviceRequired(true);
            if (strongBox) b.setIsStrongBoxBacked(true);
            switch (algorithm) {
                case 1: case 2: {
                    java.security.KeyPairGenerator g = java.security.KeyPairGenerator.getInstance(
                            algorithm == 1 ? KeyProperties.KEY_ALGORITHM_EC : KeyProperties.KEY_ALGORITHM_RSA, KEYSTORE);
                    g.initialize(b.build());
                    g.generateKeyPair();
                    break;
                }
                default: {
                    KeyGenerator g = KeyGenerator.getInstance(algorithm == 3 ? KeyProperties.KEY_ALGORITHM_HMAC_SHA256 : KeyProperties.KEY_ALGORITHM_AES, KEYSTORE);
                    g.init(b.build());
                    g.generateKey();
                    break;
                }
            }
            return true;
        } catch (Exception e) {
            Log.w("kiln", "KeyStore: cannot make " + alias + ": " + e);
            return false;
        }
    }

    static boolean deleteKey(String alias) {
        try {
            KeyStore ks = KeyStore.getInstance(KEYSTORE);
            ks.load(null);
            if (!ks.containsAlias(alias)) return false;
            ks.deleteEntry(alias);
            return true;
        } catch (Exception e) {
            return false;
        }
    }

    static boolean containsKey(String alias) {
        try {
            KeyStore ks = KeyStore.getInstance(KEYSTORE);
            ks.load(null);
            return ks.containsAlias(alias);
        } catch (Exception e) {
            return false;
        }
    }

    /** An EC or RSA key's public half (X.509); null for a secret key or none. */
    static byte[] publicKey(String alias) {
        try {
            KeyStore ks = KeyStore.getInstance(KEYSTORE);
            ks.load(null);
            java.security.cert.Certificate c = ks.getCertificate(alias);
            return c == null ? null : c.getPublicKey().getEncoded();
        } catch (Exception e) {
            return null;
        }
    }

}
