package dev.kiln;

import android.hardware.input.InputManager;
import android.os.Handler;
import android.os.Looper;
import android.view.InputDevice;

/** PrecisionPointer.android.kt: listen only while an opted-in MaterialTheme needs the devices. */
final class KilnInputDevices implements InputManager.InputDeviceListener {
    private final KilnView view;
    private final InputManager manager;
    private boolean enabled, registered;

    KilnInputDevices(KilnView view) {
        this.view = view;
        manager = view.getContext().getSystemService(InputManager.class);
    }

    void setEnabled(boolean enabled) {
        this.enabled = enabled;
        if (enabled && view.isAttachedToWindow()) onAttached();
        else stop();
    }

    void onAttached() {
        if (!enabled || registered || manager == null) return;
        registered = true;
        manager.registerInputDeviceListener(this, new Handler(Looper.getMainLooper()));
        KilnNative.clearInputDevices(view.windowId);
        for (int id : manager.getInputDeviceIds()) update(id);
    }

    void onDetached() { stop(); }

    private void stop() {
        if (!registered) return;
        registered = false;
        manager.unregisterInputDeviceListener(this);
        KilnNative.clearInputDevices(view.windowId);
    }

    private void update(int id) {
        if (!registered) return;  // ignore a queued callback after unregistering
        InputDevice device = manager.getInputDevice(id);
        KilnNative.updateInputDevice(view.windowId, id, device == null || device.isVirtual(),
                device != null ? device.getSources() : 0,
                device != null ? device.getKeyboardType() : InputDevice.KEYBOARD_TYPE_NONE);
    }

    @Override public void onInputDeviceAdded(int id) { update(id); }
    @Override public void onInputDeviceChanged(int id) { update(id); }
    @Override public void onInputDeviceRemoved(int id) { update(id); }
}
