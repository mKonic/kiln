// media::CameraHost on Android: the NDK's camera2 (ACameraManager). A camera's capture session has
// the preview's window and a JPEG image reader; the repeating request carries the zoom, torch, 3A
// modes, regions and locks; triggers and still captures are single requests with the same
// settings. Callbacks come on camera2's threads and find their camera by handle, so a closed
// camera's late callbacks find nothing.
#include "AndroidMedia.h"

#include "kiln/media/Camera.h"
#include "kiln/runtime/Task.h"

#include <android/log.h>
#include <android/native_window.h>
#include <camera/NdkCameraDevice.h>
#include <camera/NdkCameraManager.h>
#include <camera/NdkCameraMetadata.h>
#include <media/NdkImage.h>
#include <media/NdkImageReader.h>

#include <algorithm>
#include <cstring>
#include <deque>
#include <map>
#include <memory>
#include <mutex>

namespace kiln::android {

    namespace {
        ACameraManager* Manager() {
            static ACameraManager* m = ACameraManager_create();
            return m;
        }

        using Done = kiln::Function<void(std::vector<std::uint8_t>, std::string)>;

        struct Device {
            std::uint64_t handle = 0;
            ACameraDevice* device = nullptr;
            ACameraCaptureSession* session = nullptr;
            ACaptureSessionOutputContainer* outputs = nullptr;
            ACaptureSessionOutput* previewOutput = nullptr;
            ACaptureSessionOutput* jpegOutput = nullptr;
            ACameraOutputTarget* previewTarget = nullptr;
            ACameraOutputTarget* jpegTarget = nullptr;
            ACaptureRequest* repeating = nullptr;
            ANativeWindow* preview = nullptr;
            AImageReader* reader = nullptr;
            ANativeWindow* readerWindow = nullptr;   // owned by the reader
            media::CameraRequest settings;
            media::CameraHost::Callbacks callbacks;
            std::deque<Done> captures;               // waiting for their JPEG, in order
            rt::Dispatcher* main = nullptr;
            ACameraDevice_StateCallbacks deviceCallbacks{};
            ACameraCaptureSession_stateCallbacks sessionCallbacks{};
            ACameraCaptureSession_captureCallbacks resultCallbacks{};
            ACameraCaptureSession_captureCallbacks stillCallbacks{};

            void CloseSession() {
                if (session) {
                    ACameraCaptureSession_stopRepeating(session);
                    ACameraCaptureSession_close(session);
                    session = nullptr;
                }
                if (repeating) ACaptureRequest_free(std::exchange(repeating, nullptr));
                if (previewTarget) ACameraOutputTarget_free(std::exchange(previewTarget, nullptr));
                if (jpegTarget) ACameraOutputTarget_free(std::exchange(jpegTarget, nullptr));
                if (outputs) {
                    if (previewOutput) ACaptureSessionOutputContainer_remove(outputs, previewOutput);
                    if (jpegOutput) ACaptureSessionOutputContainer_remove(outputs, jpegOutput);
                    ACaptureSessionOutputContainer_free(std::exchange(outputs, nullptr));
                }
                if (previewOutput) ACaptureSessionOutput_free(std::exchange(previewOutput, nullptr));
                if (jpegOutput) ACaptureSessionOutput_free(std::exchange(jpegOutput, nullptr));
                if (preview) ANativeWindow_release(std::exchange(preview, nullptr));
            }
            ~Device() {
                CloseSession();
                if (device) ACameraDevice_close(device);
                if (reader) AImageReader_delete(reader);
            }
        };

        std::mutex g_Mutex;
        std::map<std::uint64_t, std::shared_ptr<Device>> g_Devices;
        std::uint64_t g_Next = 1;

        std::shared_ptr<Device> Find(void* context) {
            std::lock_guard lock(g_Mutex);
            auto it = g_Devices.find(reinterpret_cast<std::uintptr_t>(context));
            return it == g_Devices.end() ? nullptr : it->second;
        }
        std::shared_ptr<Device> Find(std::uint64_t handle) { return Find(reinterpret_cast<void*>(std::uintptr_t(handle))); }
        void* Context(std::uint64_t handle) { return reinterpret_cast<void*>(std::uintptr_t(handle)); }

        media::CameraState::Error StateError(camera_status_t s) {
            switch (s) {
                case ACAMERA_ERROR_CAMERA_IN_USE: return media::CameraState::Error::CameraInUse;
                case ACAMERA_ERROR_MAX_CAMERA_IN_USE: return media::CameraState::Error::MaxCamerasInUse;
                case ACAMERA_ERROR_CAMERA_DISABLED: return media::CameraState::Error::CameraDisabled;
                default: return media::CameraState::Error::CameraFatal;
            }
        }
        const char* StatusText(camera_status_t s) {
            switch (s) {
                case ACAMERA_ERROR_PERMISSION_DENIED: return "android.permission.CAMERA is not granted";
                case ACAMERA_ERROR_CAMERA_IN_USE: return "the camera is in use";
                case ACAMERA_ERROR_MAX_CAMERA_IN_USE: return "too many cameras are open";
                case ACAMERA_ERROR_CAMERA_DISABLED: return "the camera is disabled";
                case ACAMERA_ERROR_CAMERA_DISCONNECTED: return "the camera was disconnected";
                default: return "the camera failed";
            }
        }
        void ReportError(const std::shared_ptr<Device>& d, media::CameraState::Error e, std::string message) {
            __android_log_print(ANDROID_LOG_ERROR, "kiln", "camera: %s", message.c_str());
            if (d->main && d->callbacks.onError) d->main->PostFromAnyThread([f = d->callbacks.onError, e, message]() mutable { f(e, message); });
        }

        std::vector<media::CameraInfo> ListCameras() {
            std::vector<media::CameraInfo> out;
            ACameraIdList* ids = nullptr;
            if (ACameraManager_getCameraIdList(Manager(), &ids) != ACAMERA_OK || !ids) return out;
            for (int i = 0; i < ids->numCameras; ++i) {
                ACameraMetadata* meta = nullptr;
                if (ACameraManager_getCameraCharacteristics(Manager(), ids->cameraIds[i], &meta) != ACAMERA_OK) continue;
                media::CameraInfo info;
                info.id = ids->cameraIds[i];
                ACameraMetadata_const_entry e{};
                auto get = [&](std::uint32_t tag) { return ACameraMetadata_getConstEntry(meta, tag, &e) == ACAMERA_OK && e.count > 0; };
                if (get(ACAMERA_LENS_FACING))
                    info.facing = e.data.u8[0] == ACAMERA_LENS_FACING_FRONT ? media::LensFacing::Front
                                : e.data.u8[0] == ACAMERA_LENS_FACING_BACK ? media::LensFacing::Back : media::LensFacing::External;
                if (get(ACAMERA_SENSOR_ORIENTATION)) info.sensorOrientation = e.data.i32[0];
                if (get(ACAMERA_SCALER_AVAILABLE_STREAM_CONFIGURATIONS)) {
                    // (format, width, height, input) entries: the outputs a surface (the private
                    // format), a JPEG reader and a YUV reader can take.
                    for (std::uint32_t k = 0; k + 3 < e.count; k += 4) {
                        if (e.data.i32[k + 3] != ACAMERA_SCALER_AVAILABLE_STREAM_CONFIGURATIONS_OUTPUT) continue;
                        const ui::IntSize s{ e.data.i32[k + 1], e.data.i32[k + 2] };
                        if (e.data.i32[k] == AIMAGE_FORMAT_PRIVATE) info.previewSizes.push_back(s);
                        else if (e.data.i32[k] == AIMAGE_FORMAT_JPEG) info.jpegSizes.push_back(s);
                        else if (e.data.i32[k] == AIMAGE_FORMAT_YUV_420_888) info.yuvSizes.push_back(s);
                    }
                }
                if (get(ACAMERA_FLASH_INFO_AVAILABLE)) info.hasFlashUnit = e.data.u8[0] == ACAMERA_FLASH_INFO_AVAILABLE_TRUE;
                if (get(ACAMERA_SENSOR_INFO_ACTIVE_ARRAY_SIZE) && e.count >= 4)
                    info.activeArray = { e.data.i32[0], e.data.i32[1], e.data.i32[0] + e.data.i32[2], e.data.i32[1] + e.data.i32[3] };
                if (get(ACAMERA_CONTROL_ZOOM_RATIO_RANGE) && e.count >= 2) {
                    info.minZoomRatio = e.data.f[0];
                    info.maxZoomRatio = e.data.f[1];
                    info.zoomRatioControl = true;
                } else if (get(ACAMERA_SCALER_AVAILABLE_MAX_DIGITAL_ZOOM)) {
                    info.maxZoomRatio = std::max(1.0f, e.data.f[0]);
                }
                if (get(ACAMERA_CONTROL_AE_COMPENSATION_RANGE) && e.count >= 2) {
                    info.exposureCompensationMin = e.data.i32[0];
                    info.exposureCompensationMax = e.data.i32[1];
                }
                if (get(ACAMERA_CONTROL_AE_COMPENSATION_STEP)) {
                    info.exposureCompensationStepNumerator = e.data.r[0].numerator;
                    info.exposureCompensationStepDenominator = e.data.r[0].denominator;
                }
                if (get(ACAMERA_CONTROL_MAX_REGIONS) && e.count >= 3) {
                    info.maxAeRegions = e.data.i32[0];
                    info.maxAwbRegions = e.data.i32[1];
                    info.maxAfRegions = e.data.i32[2];
                }
                if (get(ACAMERA_CONTROL_AF_AVAILABLE_MODES))
                    for (std::uint32_t k = 0; k < e.count; ++k) info.afModes.push_back(e.data.u8[k]);
                if (get(ACAMERA_CONTROL_AE_AVAILABLE_MODES))
                    for (std::uint32_t k = 0; k < e.count; ++k) info.aeModes.push_back(e.data.u8[k]);
                if (get(ACAMERA_CONTROL_AE_LOCK_AVAILABLE)) info.aeLockAvailable = e.data.u8[0] == ACAMERA_CONTROL_AE_LOCK_AVAILABLE_TRUE;
                if (get(ACAMERA_CONTROL_AWB_LOCK_AVAILABLE)) info.awbLockAvailable = e.data.u8[0] == ACAMERA_CONTROL_AWB_LOCK_AVAILABLE_TRUE;
                ACameraMetadata_free(meta);
                out.push_back(std::move(info));
            }
            ACameraManager_deleteCameraIdList(ids);
            return out;
        }

        void SetRegions(ACaptureRequest* r, std::uint32_t tag, const std::optional<std::vector<media::MeteringRectangle>>& regions) {
            if (!regions) return;
            std::vector<std::int32_t> v;
            for (const auto& m : *regions) v.insert(v.end(), { m.rect.left, m.rect.top, m.rect.right, m.rect.bottom, m.weight });
            ACaptureRequest_setEntry_i32(r, tag, std::uint32_t(v.size()), v.data());
        }
        // The request's settings (CameraRequest), with the triggers idle unless given.
        void Apply(ACaptureRequest* r, const media::CameraRequest& s, std::optional<media::CameraTrigger> trigger = std::nullopt) {
            std::uint8_t u = ACAMERA_CONTROL_MODE_AUTO;
            ACaptureRequest_setEntry_u8(r, ACAMERA_CONTROL_MODE, 1, &u);
            if (s.cropRegion) {
                const std::int32_t crop[4] = { s.cropRegion->left, s.cropRegion->top, s.cropRegion->Width(), s.cropRegion->Height() };
                ACaptureRequest_setEntry_i32(r, ACAMERA_SCALER_CROP_REGION, 4, crop);
            } else {
                // CONTROL_ZOOM_RATIO (Android 11+); refused, harmlessly, by older cameras.
                ACaptureRequest_setEntry_float(r, ACAMERA_CONTROL_ZOOM_RATIO, 1, &s.zoomRatio);
            }
            u = std::uint8_t(s.afMode);
            ACaptureRequest_setEntry_u8(r, ACAMERA_CONTROL_AF_MODE, 1, &u);
            u = std::uint8_t(s.aeMode);
            ACaptureRequest_setEntry_u8(r, ACAMERA_CONTROL_AE_MODE, 1, &u);
            ACaptureRequest_setEntry_i32(r, ACAMERA_CONTROL_AE_EXPOSURE_COMPENSATION, 1, &s.aeCompensation);
            u = s.aeLock ? ACAMERA_CONTROL_AE_LOCK_ON : ACAMERA_CONTROL_AE_LOCK_OFF;
            ACaptureRequest_setEntry_u8(r, ACAMERA_CONTROL_AE_LOCK, 1, &u);
            u = s.awbLock ? ACAMERA_CONTROL_AWB_LOCK_ON : ACAMERA_CONTROL_AWB_LOCK_OFF;
            ACaptureRequest_setEntry_u8(r, ACAMERA_CONTROL_AWB_LOCK, 1, &u);
            u = s.torch ? ACAMERA_FLASH_MODE_TORCH : ACAMERA_FLASH_MODE_OFF;
            ACaptureRequest_setEntry_u8(r, ACAMERA_FLASH_MODE, 1, &u);
            SetRegions(r, ACAMERA_CONTROL_AF_REGIONS, s.afRegions);
            SetRegions(r, ACAMERA_CONTROL_AE_REGIONS, s.aeRegions);
            SetRegions(r, ACAMERA_CONTROL_AWB_REGIONS, s.awbRegions);
            std::uint8_t af = ACAMERA_CONTROL_AF_TRIGGER_IDLE, ae = ACAMERA_CONTROL_AE_PRECAPTURE_TRIGGER_IDLE;
            if (trigger == media::CameraTrigger::AfStart) af = ACAMERA_CONTROL_AF_TRIGGER_START;
            if (trigger == media::CameraTrigger::AfCancel) af = ACAMERA_CONTROL_AF_TRIGGER_CANCEL;
            if (trigger == media::CameraTrigger::AePrecaptureStart) ae = ACAMERA_CONTROL_AE_PRECAPTURE_TRIGGER_START;
            if (trigger == media::CameraTrigger::AePrecaptureCancel) ae = ACAMERA_CONTROL_AE_PRECAPTURE_TRIGGER_CANCEL;
            ACaptureRequest_setEntry_u8(r, ACAMERA_CONTROL_AF_TRIGGER, 1, &af);
            ACaptureRequest_setEntry_u8(r, ACAMERA_CONTROL_AE_PRECAPTURE_TRIGGER, 1, &ae);
        }

        // Each completed frame's 3A states, for the controls and the capture pipeline.
        void OnCompleted(void* context, ACameraCaptureSession*, ACaptureRequest*, const ACameraMetadata* result) {
            auto d = Find(context);
            if (!d || !d->main || !d->callbacks.onResult) return;
            media::CameraFrameResult r;
            ACameraMetadata_const_entry e{};
            if (ACameraMetadata_getConstEntry(result, ACAMERA_CONTROL_AF_STATE, &e) == ACAMERA_OK && e.count) r.afState = e.data.u8[0];
            if (ACameraMetadata_getConstEntry(result, ACAMERA_CONTROL_AE_STATE, &e) == ACAMERA_OK && e.count) r.aeState = e.data.u8[0];
            if (ACameraMetadata_getConstEntry(result, ACAMERA_CONTROL_AWB_STATE, &e) == ACAMERA_OK && e.count) r.awbState = e.data.u8[0];
            d->main->PostFromAnyThread([f = d->callbacks.onResult, r]() mutable { f(r); });
        }
        void OnStillFailed(void* context, ACameraCaptureSession*, ACaptureRequest*, ACameraCaptureFailure* failure) {
            auto d = Find(context);
            if (!d) return;
            Done done;
            {
                std::lock_guard lock(g_Mutex);
                if (d->captures.empty()) return;
                done = std::move(d->captures.front());
                d->captures.pop_front();
            }
            const std::string why = "the capture failed (" + std::to_string(failure ? failure->reason : -1) + ")";
            d->main->PostFromAnyThread([done = std::move(done), why]() mutable { done({}, why); });
        }
        void OnJpeg(void* context, AImageReader* reader) {
            AImage* image = nullptr;
            if (AImageReader_acquireNextImage(reader, &image) != AMEDIA_OK || !image) return;
            std::uint8_t* data = nullptr;
            int length = 0;
            std::vector<std::uint8_t> jpeg;
            if (AImage_getPlaneData(image, 0, &data, &length) == AMEDIA_OK && data && length > 0) jpeg.assign(data, data + length);
            AImage_delete(image);
            auto d = Find(context);
            if (!d) return;
            Done done;
            {
                std::lock_guard lock(g_Mutex);
                if (d->captures.empty()) return;
                done = std::move(d->captures.front());
                d->captures.pop_front();
            }
            d->main->PostFromAnyThread([done = std::move(done), jpeg = std::move(jpeg)]() mutable { done(std::move(jpeg), {}); });
        }

        std::uint64_t Open(const std::string& id, media::CameraHost::Callbacks callbacks) {
            auto d = std::make_shared<Device>();
            d->callbacks = std::move(callbacks);
            d->main = &rt::Dispatcher::Main();
            {
                std::lock_guard lock(g_Mutex);
                d->handle = g_Next++;
                g_Devices[d->handle] = d;
            }
            void* ctx = Context(d->handle);
            d->deviceCallbacks.context = ctx;
            d->deviceCallbacks.onDisconnected = [](void* c, ACameraDevice*) {
                if (auto d = Find(c)) ReportError(d, media::CameraState::Error::CameraInUse, "the camera was disconnected");
            };
            d->deviceCallbacks.onError = [](void* c, ACameraDevice*, int error) {
                // StateCallback.ERROR_*: 1 in use, 2 max in use, 3 disabled, 4 device, 5 service.
                const auto e = error == 1 ? media::CameraState::Error::CameraInUse
                             : error == 2 ? media::CameraState::Error::MaxCamerasInUse
                             : error == 3 ? media::CameraState::Error::CameraDisabled
                             : media::CameraState::Error::CameraFatal;
                if (auto d = Find(c)) ReportError(d, e, "the camera failed (" + std::to_string(error) + ")");
            };
            const camera_status_t s = ACameraManager_openCamera(Manager(), id.c_str(), &d->deviceCallbacks, &d->device);
            if (s != ACAMERA_OK) {
                ReportError(d, StateError(s), StatusText(s));
                return d->handle;   // the error arrives; Close forgets it
            }
            // Opened (the NDK opens synchronously); reported after the caller has the handle.
            d->main->Post([f = d->callbacks.onOpened]() mutable { if (f) f(); });
            return d->handle;
        }

        bool Configure(std::uint64_t handle, const media::CameraOutputs& outputs, const media::CameraRequest& request) {
            auto d = Find(handle);
            if (!d || !d->device) return false;
            d->CloseSession();
            d->settings = request;
            void* ctx = Context(handle);
            if (outputs.jpegSize.width > 0 && !d->reader) {
                if (AImageReader_new(outputs.jpegSize.width, outputs.jpegSize.height, AIMAGE_FORMAT_JPEG, 2, &d->reader) != AMEDIA_OK) return false;
                AImageReader_ImageListener listener{ ctx, &OnJpeg };
                AImageReader_setImageListener(d->reader, &listener);
                AImageReader_getWindow(d->reader, &d->readerWindow);
            }
            if (!outputs.preview.nativeWindow && !d->readerWindow) return true;   // nothing to stream to yet
            if (ACaptureSessionOutputContainer_create(&d->outputs) != ACAMERA_OK) return false;
            if (outputs.preview.nativeWindow) {
                d->preview = static_cast<ANativeWindow*>(outputs.preview.nativeWindow);
                ANativeWindow_acquire(d->preview);
                if (ACaptureSessionOutput_create(d->preview, &d->previewOutput) != ACAMERA_OK
                    || ACaptureSessionOutputContainer_add(d->outputs, d->previewOutput) != ACAMERA_OK
                    || ACameraOutputTarget_create(d->preview, &d->previewTarget) != ACAMERA_OK)
                    return false;
            }
            if (d->readerWindow) {
                if (ACaptureSessionOutput_create(d->readerWindow, &d->jpegOutput) != ACAMERA_OK
                    || ACaptureSessionOutputContainer_add(d->outputs, d->jpegOutput) != ACAMERA_OK
                    || ACameraOutputTarget_create(d->readerWindow, &d->jpegTarget) != ACAMERA_OK)
                    return false;
            }
            d->sessionCallbacks.context = ctx;
            d->sessionCallbacks.onClosed = [](void*, ACameraCaptureSession*) {};
            d->sessionCallbacks.onReady = [](void*, ACameraCaptureSession*) {};
            d->sessionCallbacks.onActive = [](void*, ACameraCaptureSession*) {};
            camera_status_t s = ACameraDevice_createCaptureSession(d->device, d->outputs, &d->sessionCallbacks, &d->session);
            if (s != ACAMERA_OK) {
                ReportError(d, media::CameraState::Error::StreamConfig, StatusText(s));
                return false;
            }
            d->resultCallbacks = {};
            d->resultCallbacks.context = ctx;
            d->resultCallbacks.onCaptureCompleted = &OnCompleted;
            d->stillCallbacks = d->resultCallbacks;
            d->stillCallbacks.onCaptureFailed = &OnStillFailed;
            if (d->previewTarget) {
                if (ACameraDevice_createCaptureRequest(d->device, TEMPLATE_PREVIEW, &d->repeating) != ACAMERA_OK) return false;
                ACaptureRequest_addTarget(d->repeating, d->previewTarget);
                Apply(d->repeating, request);
                s = ACameraCaptureSession_setRepeatingRequest(d->session, &d->resultCallbacks, 1, &d->repeating, nullptr);
                if (s != ACAMERA_OK) ReportError(d, media::CameraState::Error::OtherRecoverable, StatusText(s));
            }
            return true;
        }

        void SetRequest(std::uint64_t handle, const media::CameraRequest& request) {
            auto d = Find(handle);
            if (!d) return;
            d->settings = request;
            if (!d->session || !d->repeating) return;
            Apply(d->repeating, request);
            ACameraCaptureSession_setRepeatingRequest(d->session, &d->resultCallbacks, 1, &d->repeating, nullptr);
        }

        void Trigger(std::uint64_t handle, const media::CameraRequest& request, media::CameraTrigger trigger) {
            auto d = Find(handle);
            if (!d || !d->session || !d->previewTarget) return;
            ACaptureRequest* r = nullptr;
            if (ACameraDevice_createCaptureRequest(d->device, TEMPLATE_PREVIEW, &r) != ACAMERA_OK) return;
            ACaptureRequest_addTarget(r, d->previewTarget);
            Apply(r, request, trigger);
            ACameraCaptureSession_capture(d->session, &d->resultCallbacks, 1, &r, nullptr);
            ACaptureRequest_free(r);
        }

        void Capture(std::uint64_t handle, const media::CameraRequest& request, int quality, int orientation, Done done) {
            auto d = Find(handle);
            if (!d || !d->session || !d->jpegTarget) {
                if (done) done({}, "the camera has no capture stream");
                return;
            }
            ACaptureRequest* r = nullptr;
            if (ACameraDevice_createCaptureRequest(d->device, TEMPLATE_STILL_CAPTURE, &r) != ACAMERA_OK) {
                done({}, "cannot make a capture request");
                return;
            }
            ACaptureRequest_addTarget(r, d->jpegTarget);
            if (d->previewTarget) ACaptureRequest_addTarget(r, d->previewTarget);
            Apply(r, request);
            const std::uint8_t q = std::uint8_t(std::clamp(quality, 1, 100));
            ACaptureRequest_setEntry_u8(r, ACAMERA_JPEG_QUALITY, 1, &q);
            const std::int32_t o = orientation;
            ACaptureRequest_setEntry_i32(r, ACAMERA_JPEG_ORIENTATION, 1, &o);
            {
                std::lock_guard lock(g_Mutex);
                d->captures.push_back(std::move(done));
            }
            const camera_status_t s = ACameraCaptureSession_capture(d->session, &d->stillCallbacks, 1, &r, nullptr);
            ACaptureRequest_free(r);
            if (s != ACAMERA_OK) {
                Done failed;
                {
                    std::lock_guard lock(g_Mutex);
                    if (!d->captures.empty()) {
                        failed = std::move(d->captures.back());
                        d->captures.pop_back();
                    }
                }
                if (failed) failed({}, StatusText(s));
            }
        }

        void Close(std::uint64_t handle) {
            std::shared_ptr<Device> d;
            {
                std::lock_guard lock(g_Mutex);
                auto it = g_Devices.find(handle);
                if (it == g_Devices.end()) return;
                d = std::move(it->second);
                g_Devices.erase(it);
            }
            // Closing the session and device here (synchronously) frees the windows; callbacks
            // still in flight find no device.
        }
    }

    void InstallCamera() {
        media::CameraHost h;
        h.cameras = ListCameras;
        h.open = Open;
        h.configure = Configure;
        h.setRequest = SetRequest;
        h.trigger = Trigger;
        h.capture = Capture;
        h.close = Close;
        media::SetCameraHost(std::move(h));
    }

}
