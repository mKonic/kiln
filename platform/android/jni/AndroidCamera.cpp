// media::CameraHost on Android: the NDK's camera2 (ACameraManager). A camera's capture session has
// the preview's window and a JPEG image reader; the repeating request carries the zoom, torch, 3A
// modes, regions and locks; triggers and still captures are single requests with the same
// settings. Callbacks come on camera2's threads and find their camera by handle, so a closed
// camera's late callbacks find nothing.
#include "AndroidMedia.h"

#include "kiln/media/Camera.h"
#include "kiln/runtime/Task.h"

#include <aaudio/AAudio.h>
#include <android/log.h>
#include <android/native_window.h>
#include <camera/NdkCameraDevice.h>
#include <camera/NdkCameraManager.h>
#include <camera/NdkCameraMetadata.h>
#include <media/NdkImage.h>
#include <media/NdkImageReader.h>
#include <media/NdkMediaCodec.h>
#include <media/NdkMediaMuxer.h>

#include <atomic>
#include <cmath>
#include <ctime>
#include <fcntl.h>
#include <thread>
#include <unistd.h>

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
            AImageReader* analysis = nullptr;        // YUV_420_888 frames for ImageAnalysis
            ANativeWindow* analysisWindow = nullptr;
            ACaptureSessionOutput* analysisOutput = nullptr;
            ACameraOutputTarget* analysisTarget = nullptr;
            // A recording's encoder surface, while it records.
            ANativeWindow* recordWindow = nullptr;
            ACaptureSessionOutput* recordOutput = nullptr;
            ACameraOutputTarget* recordTarget = nullptr;
            media::CameraOutputs lastOutputs;
            bool realtimeTimestamps = false;         // SENSOR_INFO_TIMESTAMP_SOURCE: CLOCK_BOOTTIME
            std::atomic<bool> lost{ false };          // disconnected or failed: no more sessions
            media::CameraRequest settings;
            media::CameraHost::Callbacks callbacks;
            std::deque<Done> captures;               // waiting for their JPEG, in order
            rt::Dispatcher* main = nullptr;
            ACameraDevice_StateCallbacks deviceCallbacks{};
            ACameraCaptureSession_stateCallbacks sessionCallbacks{};
            ACameraCaptureSession_captureCallbacks resultCallbacks{};
            ACameraCaptureSession_captureCallbacks stillCallbacks{};
            // Held by the image readers' callbacks while they read an image; Close takes it to
            // mark the device closed before the readers go.
            std::mutex readerMutex;
            bool closed = false;

            void CloseSession() {
                if (session) {
                    ACameraCaptureSession_stopRepeating(session);
                    ACameraCaptureSession_close(session);
                    session = nullptr;
                }
                if (repeating) ACaptureRequest_free(std::exchange(repeating, nullptr));
                if (previewTarget) ACameraOutputTarget_free(std::exchange(previewTarget, nullptr));
                if (analysisTarget) ACameraOutputTarget_free(std::exchange(analysisTarget, nullptr));
                if (recordTarget) ACameraOutputTarget_free(std::exchange(recordTarget, nullptr));
                if (jpegTarget) ACameraOutputTarget_free(std::exchange(jpegTarget, nullptr));
                if (outputs) {
                    if (previewOutput) ACaptureSessionOutputContainer_remove(outputs, previewOutput);
                    if (jpegOutput) ACaptureSessionOutputContainer_remove(outputs, jpegOutput);
                    if (analysisOutput) ACaptureSessionOutputContainer_remove(outputs, analysisOutput);
                    if (recordOutput) ACaptureSessionOutputContainer_remove(outputs, recordOutput);
                    ACaptureSessionOutputContainer_free(std::exchange(outputs, nullptr));
                }
                if (previewOutput) ACaptureSessionOutput_free(std::exchange(previewOutput, nullptr));
                if (jpegOutput) ACaptureSessionOutput_free(std::exchange(jpegOutput, nullptr));
                if (analysisOutput) ACaptureSessionOutput_free(std::exchange(analysisOutput, nullptr));
                if (recordOutput) ACaptureSessionOutput_free(std::exchange(recordOutput, nullptr));
                if (preview) ANativeWindow_release(std::exchange(preview, nullptr));
            }
            // On the main thread (Close): the session, the device, then the readers. A callback
            // may still hold the Device (plain data then), never its camera2 objects.
            void Shutdown() {
                {
                    std::lock_guard lock(readerMutex);
                    closed = true;
                }
                CloseSession();
                if (device) ACameraDevice_close(std::exchange(device, nullptr));
                if (reader) AImageReader_delete(std::exchange(reader, nullptr));
                if (analysis) AImageReader_delete(std::exchange(analysis, nullptr));
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
            if (e != media::CameraState::Error::OtherRecoverable && e != media::CameraState::Error::StreamConfig) d->lost = true;
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
            auto owner = Find(context);
            if (!owner) return;
            std::unique_lock guard(owner->readerMutex);
            if (owner->closed) return;
            AImage* image = nullptr;
            if (AImageReader_acquireNextImage(reader, &image) != AMEDIA_OK || !image) return;
            std::uint8_t* data = nullptr;
            int length = 0;
            std::vector<std::uint8_t> jpeg;
            if (AImage_getPlaneData(image, 0, &data, &length) == AMEDIA_OK && data && length > 0) jpeg.assign(data, data + length);
            AImage_delete(image);
            guard.unlock();
            auto& d = owner;
            Done done;
            {
                std::lock_guard lock(g_Mutex);
                if (d->captures.empty()) return;
                done = std::move(d->captures.front());
                d->captures.pop_front();
            }
            d->main->PostFromAnyThread([done = std::move(done), jpeg = std::move(jpeg)]() mutable { done(std::move(jpeg), {}); });
        }

        // The latest YUV frame to the analyzer, on the reader's thread (older ones are dropped).
        void OnAnalysis(void* context, AImageReader* reader) {
            auto d = Find(context);
            if (!d) return;
            std::lock_guard guard(d->readerMutex);
            if (d->closed) return;
            AImage* image = nullptr;
            if (AImageReader_acquireLatestImage(reader, &image) != AMEDIA_OK || !image) return;
            if (d && d->callbacks.onAnalysisFrame) {
                media::ImageFrame f;
                std::int32_t w = 0, h = 0, planes = 0;
                std::int64_t ts = 0;
                AImage_getWidth(image, &w);
                AImage_getHeight(image, &h);
                AImage_getNumberOfPlanes(image, &planes);
                AImage_getTimestamp(image, &ts);
                f.width = w;
                f.height = h;
                f.timestampNs = ts;
                f.planeCount = std::min(planes, 3);
                for (int i = 0; i < f.planeCount; ++i) {
                    std::uint8_t* data = nullptr;
                    int length = 0;
                    std::int32_t row = 0, pixel = 0;
                    AImage_getPlaneData(image, i, &data, &length);
                    AImage_getPlaneRowStride(image, i, &row);
                    AImage_getPlanePixelStride(image, i, &pixel);
                    f.planes[std::size_t(i)] = { data, std::size_t(length), row, pixel };
                }
                d->callbacks.onAnalysisFrame(f);
            }
            AImage_delete(image);
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
            ACameraMetadata* meta = nullptr;
            if (ACameraManager_getCameraCharacteristics(Manager(), id.c_str(), &meta) == ACAMERA_OK) {
                ACameraMetadata_const_entry e{};
                if (ACameraMetadata_getConstEntry(meta, ACAMERA_SENSOR_INFO_TIMESTAMP_SOURCE, &e) == ACAMERA_OK && e.count)
                    d->realtimeTimestamps = e.data.u8[0] == ACAMERA_SENSOR_INFO_TIMESTAMP_SOURCE_REALTIME;
                ACameraMetadata_free(meta);
            }
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
            if (!d || !d->device || d->lost) return false;
            d->CloseSession();
            d->settings = request;
            d->lastOutputs = outputs;
            void* ctx = Context(handle);
            if (outputs.jpegSize.width > 0 && !d->reader) {
                if (AImageReader_new(outputs.jpegSize.width, outputs.jpegSize.height, AIMAGE_FORMAT_JPEG, 2, &d->reader) != AMEDIA_OK) return false;
                AImageReader_ImageListener listener{ ctx, &OnJpeg };
                AImageReader_setImageListener(d->reader, &listener);
                AImageReader_getWindow(d->reader, &d->readerWindow);
            }
            if (outputs.analysisSize.width > 0 && !d->analysis) {
                if (AImageReader_new(outputs.analysisSize.width, outputs.analysisSize.height, AIMAGE_FORMAT_YUV_420_888, 2, &d->analysis) != AMEDIA_OK)
                    return false;
                AImageReader_ImageListener listener{ ctx, &OnAnalysis };
                AImageReader_setImageListener(d->analysis, &listener);
                AImageReader_getWindow(d->analysis, &d->analysisWindow);
            }
            if (!outputs.preview.nativeWindow && !d->readerWindow && !d->analysisWindow && !d->recordWindow) return true;   // nothing to stream to yet
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
            if (d->analysisWindow) {
                if (ACaptureSessionOutput_create(d->analysisWindow, &d->analysisOutput) != ACAMERA_OK
                    || ACaptureSessionOutputContainer_add(d->outputs, d->analysisOutput) != ACAMERA_OK
                    || ACameraOutputTarget_create(d->analysisWindow, &d->analysisTarget) != ACAMERA_OK)
                    return false;
            }
            if (d->recordWindow) {
                if (ACaptureSessionOutput_create(d->recordWindow, &d->recordOutput) != ACAMERA_OK
                    || ACaptureSessionOutputContainer_add(d->outputs, d->recordOutput) != ACAMERA_OK
                    || ACameraOutputTarget_create(d->recordWindow, &d->recordTarget) != ACAMERA_OK)
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
            if (d->previewTarget || d->analysisTarget || d->recordTarget) {
                if (ACameraDevice_createCaptureRequest(d->device, TEMPLATE_PREVIEW, &d->repeating) != ACAMERA_OK) return false;
                if (d->previewTarget) ACaptureRequest_addTarget(d->repeating, d->previewTarget);
                if (d->analysisTarget) ACaptureRequest_addTarget(d->repeating, d->analysisTarget);
                if (d->recordTarget) ACaptureRequest_addTarget(d->repeating, d->recordTarget);
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
            if (!d || !d->session || (!d->previewTarget && !d->analysisTarget)) return;
            ACaptureRequest* r = nullptr;
            if (ACameraDevice_createCaptureRequest(d->device, TEMPLATE_PREVIEW, &r) != ACAMERA_OK) return;
            ACaptureRequest_addTarget(r, d->previewTarget ? d->previewTarget : d->analysisTarget);
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

        // --- recording (Recorder: an H.264 encoder fed by the camera, AAC from the microphone,
        // an MP4 muxer) --------------------------------------------------------------------------

        struct Recorder : std::enable_shared_from_this<Recorder> {
            std::uint64_t id = 0, camera = 0;
            media::RecordingConfig config;
            media::RecordingCallbacks callbacks;
            rt::Dispatcher* main = nullptr;
            clockid_t clock = CLOCK_MONOTONIC;       // the camera's timestamps' clock
            AMediaCodec* video = nullptr;
            AMediaCodec* audio = nullptr;
            ANativeWindow* input = nullptr;
            AAudioStream* mic = nullptr;
            AMediaMuxer* muxer = nullptr;
            int fd = -1;
            std::thread videoThread, audioThread;
            std::atomic<bool> stopping{ false }, muted{ false };
            // Pause: samples between pausing and resuming are dropped and the time skipped.
            std::mutex timeMutex;
            bool paused = false;
            std::int64_t pauseStartUs = 0, pausedTotalUs = 0;
            std::vector<std::pair<std::int64_t, std::int64_t>> pauses;   // [start, end) in source time
            // The muxer starts once every track has its format; samples wait until then.
            std::mutex muxMutex;
            int videoTrack = -1, audioTrack = -1;
            bool started = false, audioEnabled = false;
            struct Pending { int track; std::vector<std::uint8_t> data; AMediaCodecBufferInfo info; };
            std::vector<Pending> pending;
            std::int64_t firstUs = -1, lastUs = 0, bytes = 0;
            bool videoWritten = false, reportedStart = false;
            std::atomic<int> amplitude{ 0 };          // 0..32767
            media::RecordingStats::AudioState audioState = media::RecordingStats::AudioState::Disabled;
            media::VideoRecordEvent::Error error = media::VideoRecordEvent::Error::None;
            std::string errorMessage;

            std::int64_t NowUs() const {
                timespec t{};
                clock_gettime(clock, &t);
                return std::int64_t(t.tv_sec) * 1'000'000 + t.tv_nsec / 1000;
            }
            // A sample's time in the file: before any pause it ends at, less the paused time; in
            // a pause: dropped.
            std::optional<std::int64_t> MapTime(std::int64_t us) {
                std::lock_guard lock(timeMutex);
                std::int64_t skipped = 0;
                for (auto [a, b] : pauses) {
                    if (us >= a && us < b) return std::nullopt;
                    if (us >= b) skipped += b - a;
                }
                if (paused && us >= pauseStartUs) return std::nullopt;
                return us - skipped;
            }

            void Write(int track, const std::uint8_t* data, const AMediaCodecBufferInfo& info) {
                std::lock_guard lock(muxMutex);
                if (!started) {
                    pending.push_back({ track, std::vector<std::uint8_t>(data + info.offset, data + info.offset + info.size), info });
                    pending.back().info.offset = 0;
                    return;
                }
                WriteLocked(track, data, info);
            }
            void WriteLocked(int track, const std::uint8_t* data, const AMediaCodecBufferInfo& info) {
                if (track == videoTrack) videoWritten = true;
                if (firstUs < 0) firstUs = info.presentationTimeUs;
                AMediaCodecBufferInfo i = info;
                i.presentationTimeUs = std::max<std::int64_t>(0, info.presentationTimeUs - firstUs);
                if (AMediaMuxer_writeSampleData(muxer, std::size_t(track), data, &i) != AMEDIA_OK) return;
                bytes += info.size;
                lastUs = std::max(lastUs, i.presentationTimeUs);
                if (track != videoTrack) return;
                media::RecordingStats st;
                st.recordedDurationNanos = lastUs * 1000;
                st.numBytesRecorded = bytes;
                st.audioState = audioState;
                st.audioAmplitude = double(amplitude.load()) / 32767.0;
                const bool first = !reportedStart;
                reportedStart = true;
                main->PostFromAnyThread([cb = callbacks.onStarted, status = callbacks.onStatus, st, first]() mutable {
                    if (first && cb) cb();
                    if (status) status(st);
                });
            }
            void StartMuxerIfReady() {
                // Under muxMutex.
                if (started || videoTrack < 0 || (audioEnabled && audioTrack < 0)) return;
                if (AMediaMuxer_start(muxer) != AMEDIA_OK) {
                    error = media::VideoRecordEvent::Error::RecorderError;
                    errorMessage = "the muxer cannot start";
                    return;
                }
                started = true;
                // Samples in time order across the tracks.
                std::stable_sort(pending.begin(), pending.end(), [](const Pending& a, const Pending& b) {
                    return a.info.presentationTimeUs < b.info.presentationTimeUs;
                });
                // The file starts with a video key frame.
                bool keyFrameSeen = false;
                for (auto& p : pending) {
                    if (p.track == videoTrack && (p.info.flags & 1 /* KEY_FRAME */)) keyFrameSeen = true;
                    if (!keyFrameSeen) continue;
                    WriteLocked(p.track, p.data.data(), p.info);
                }
                pending.clear();
            }
            void AddTrack(AMediaCodec* codec, bool isVideo) {
                AMediaFormat* format = AMediaCodec_getOutputFormat(codec);
                std::lock_guard lock(muxMutex);
                const ssize_t track = AMediaMuxer_addTrack(muxer, format);
                AMediaFormat_delete(format);
                if (isVideo) videoTrack = int(track);
                else audioTrack = int(track);
                StartMuxerIfReady();
            }
            // Drains an encoder's output into the muxer; true at the end of the stream.
            bool Drain(AMediaCodec* codec, bool isVideo, std::int64_t timeoutUs) {
                AMediaCodecBufferInfo info{};
                const ssize_t index = AMediaCodec_dequeueOutputBuffer(codec, &info, timeoutUs);
                if (index == AMEDIACODEC_INFO_OUTPUT_FORMAT_CHANGED) {
                    AddTrack(codec, isVideo);
                    return false;
                }
                if (index < 0) return false;
                std::size_t size = 0;
                std::uint8_t* data = AMediaCodec_getOutputBuffer(codec, std::size_t(index), &size);
                const bool end = info.flags & AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM;
                if (data && info.size > 0 && !(info.flags & AMEDIACODEC_BUFFER_FLAG_CODEC_CONFIG)) {
                    if (auto t = MapTime(info.presentationTimeUs)) {
                        info.presentationTimeUs = *t;
                        Write(isVideo ? videoTrack : audioTrack, data, info);
                    }
                }
                AMediaCodec_releaseOutputBuffer(codec, std::size_t(index), false);
                return end;
            }

            void VideoLoop() {
                while (!Drain(video, true, 10'000)) {}
            }
            void AudioLoop() {
                const int frames = config.sampleRate / 50;   // 20 ms
                std::vector<std::int16_t> buffer(std::size_t(frames * config.channels));
                std::int64_t framesRead = 0;
                bool ended = false;
                while (!ended) {
                    const bool last = stopping.load();
                    aaudio_result_t n = mic ? AAudioStream_read(mic, buffer.data(), frames, 100'000'000) : 0;
                    if (n < 0) {
                        audioState = media::RecordingStats::AudioState::SourceError;
                        n = 0;
                    }
                    if (n > 0) {
                        // The time of the first frame read: the stream's timestamp mapped to it.
                        std::int64_t position = 0, timeNs = 0;
                        std::int64_t us;
                        if (AAudioStream_getTimestamp(mic, clock, &position, &timeNs) == AAUDIO_OK)
                            us = timeNs / 1000 + (framesRead - position) * 1'000'000 / config.sampleRate;
                        else
                            us = NowUs() - std::int64_t(n) * 1'000'000 / config.sampleRate;
                        framesRead += n;
                        int peak = 0;
                        for (aaudio_result_t i = 0; i < n * config.channels; ++i) peak = std::max(peak, std::abs(int(buffer[std::size_t(i)])));
                        amplitude = std::min(peak, 32767);
                        if (muted.load()) std::fill(buffer.begin(), buffer.begin() + n * config.channels, 0);
                        Feed(reinterpret_cast<const std::uint8_t*>(buffer.data()), std::size_t(n) * config.channels * 2, us, false);
                    }
                    if (last) {
                        Feed(nullptr, 0, NowUs(), true);
                        while (!Drain(audio, false, 10'000)) {}
                        ended = true;
                    } else {
                        while (DrainOnce()) {}
                    }
                }
            }
            bool DrainOnce() {
                AMediaCodecBufferInfo info{};
                const ssize_t index = AMediaCodec_dequeueOutputBuffer(audio, &info, 0);
                if (index == AMEDIACODEC_INFO_OUTPUT_FORMAT_CHANGED) {
                    AddTrack(audio, false);
                    return true;
                }
                if (index < 0) return false;
                std::size_t size = 0;
                std::uint8_t* data = AMediaCodec_getOutputBuffer(audio, std::size_t(index), &size);
                if (data && info.size > 0 && !(info.flags & AMEDIACODEC_BUFFER_FLAG_CODEC_CONFIG))
                    if (auto t = MapTime(info.presentationTimeUs)) {
                        info.presentationTimeUs = *t;
                        Write(audioTrack, data, info);
                    }
                AMediaCodec_releaseOutputBuffer(audio, std::size_t(index), false);
                return true;
            }
            void Feed(const std::uint8_t* pcm, std::size_t length, std::int64_t us, bool end) {
                std::size_t done = 0;
                do {
                    const ssize_t index = AMediaCodec_dequeueInputBuffer(audio, 10'000);
                    if (index < 0) {
                        if (stopping.load() && !end) return;
                        DrainOnce();
                        continue;
                    }
                    std::size_t capacity = 0;
                    std::uint8_t* in = AMediaCodec_getInputBuffer(audio, std::size_t(index), &capacity);
                    const std::size_t n = std::min(capacity, length - done);
                    if (n && in) std::memcpy(in, pcm + done, n);
                    const std::int64_t t = us + std::int64_t(done / (2 * config.channels)) * 1'000'000 / config.sampleRate;
                    done += n;
                    AMediaCodec_queueInputBuffer(audio, std::size_t(index), 0, n, std::uint64_t(t),
                                                 end && done >= length ? AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM : 0);
                } while (done < length);
            }

            ~Recorder() {
                if (video) AMediaCodec_delete(video);
                if (audio) AMediaCodec_delete(audio);
                if (input) ANativeWindow_release(input);
                if (mic) AAudioStream_close(mic);
                if (muxer) AMediaMuxer_delete(muxer);
                if (fd >= 0) close(fd);
            }
        };

        std::map<std::uint64_t, std::shared_ptr<Recorder>> g_Recorders;   // main thread
        std::uint64_t g_NextRecorder = 1;

        AMediaCodec* MakeEncoder(const char* mime, AMediaFormat* format, ANativeWindow** surface) {
            AMediaCodec* c = AMediaCodec_createEncoderByType(mime);
            if (!c) return nullptr;
            if (AMediaCodec_configure(c, format, nullptr, nullptr, AMEDIACODEC_CONFIGURE_FLAG_ENCODE) != AMEDIA_OK
                || (surface && AMediaCodec_createInputSurface(c, surface) != AMEDIA_OK) || AMediaCodec_start(c) != AMEDIA_OK) {
                AMediaCodec_delete(c);
                return nullptr;
            }
            return c;
        }

        std::uint64_t StartRecording(std::uint64_t camera, const media::RecordingConfig& config, media::RecordingCallbacks callbacks) {
            auto d = Find(camera);
            if (!d || !d->device) return 0;
            auto r = std::make_shared<Recorder>();
            r->id = g_NextRecorder++;
            r->camera = camera;
            r->config = config;
            r->callbacks = std::move(callbacks);
            r->main = d->main;
            r->clock = d->realtimeTimestamps ? CLOCK_BOOTTIME : CLOCK_MONOTONIC;
            r->muted = config.muted;
            // The video encoder (VideoEncoderConfig): a surface input, a key frame a second.
            AMediaFormat* vf = AMediaFormat_new();
            AMediaFormat_setString(vf, AMEDIAFORMAT_KEY_MIME, "video/avc");
            AMediaFormat_setInt32(vf, AMEDIAFORMAT_KEY_WIDTH, config.size.width);
            AMediaFormat_setInt32(vf, AMEDIAFORMAT_KEY_HEIGHT, config.size.height);
            AMediaFormat_setInt32(vf, AMEDIAFORMAT_KEY_BIT_RATE, config.videoBitrate);
            AMediaFormat_setInt32(vf, AMEDIAFORMAT_KEY_FRAME_RATE, config.frameRate);
            AMediaFormat_setInt32(vf, AMEDIAFORMAT_KEY_I_FRAME_INTERVAL, 1);
            AMediaFormat_setInt32(vf, AMEDIAFORMAT_KEY_COLOR_FORMAT, 0x7F000789);   // COLOR_FormatSurface
            r->video = MakeEncoder("video/avc", vf, &r->input);
            AMediaFormat_delete(vf);
            if (!r->video) {
                __android_log_print(ANDROID_LOG_ERROR, "kiln", "camera: no H.264 encoder for %dx%d", config.size.width, config.size.height);
                return 0;
            }
            r->fd = open(config.file.c_str(), O_CREAT | O_TRUNC | O_RDWR | O_CLOEXEC, 0644);
            if (r->fd < 0 || !(r->muxer = AMediaMuxer_new(r->fd, AMEDIAMUXER_OUTPUT_FORMAT_MPEG_4))) {
                __android_log_print(ANDROID_LOG_ERROR, "kiln", "camera: cannot write %s", config.file.c_str());
                return 0;
            }
            AMediaMuxer_setOrientationHint(r->muxer, config.orientationHint);
            if (config.audio) {
                // The microphone (AudioSource: MIC, 16-bit PCM) and AAC-LC.
                AAudioStreamBuilder* b = nullptr;
                if (AAudio_createStreamBuilder(&b) == AAUDIO_OK) {
                    AAudioStreamBuilder_setDirection(b, AAUDIO_DIRECTION_INPUT);
                    AAudioStreamBuilder_setFormat(b, AAUDIO_FORMAT_PCM_I16);
                    AAudioStreamBuilder_setSampleRate(b, config.sampleRate);
                    AAudioStreamBuilder_setChannelCount(b, config.channels);
                    AAudioStreamBuilder_setInputPreset(b, AAUDIO_INPUT_PRESET_CAMCORDER);
                    if (AAudioStreamBuilder_openStream(b, &r->mic) != AAUDIO_OK) r->mic = nullptr;
                    AAudioStreamBuilder_delete(b);
                }
                AMediaFormat* af = AMediaFormat_new();
                AMediaFormat_setString(af, AMEDIAFORMAT_KEY_MIME, "audio/mp4a-latm");
                AMediaFormat_setInt32(af, AMEDIAFORMAT_KEY_SAMPLE_RATE, config.sampleRate);
                AMediaFormat_setInt32(af, AMEDIAFORMAT_KEY_CHANNEL_COUNT, config.channels);
                AMediaFormat_setInt32(af, AMEDIAFORMAT_KEY_BIT_RATE, config.audioBitrate);
                AMediaFormat_setInt32(af, AMEDIAFORMAT_KEY_AAC_PROFILE, 2);   // AAC LC
                if (r->mic && AAudioStream_requestStart(r->mic) == AAUDIO_OK) r->audio = MakeEncoder("audio/mp4a-latm", af, nullptr);
                AMediaFormat_delete(af);
                r->audioEnabled = r->audio != nullptr;
                // AudioStats: the source failed, the recording goes on without it.
                r->audioState = r->audioEnabled ? (config.muted ? media::RecordingStats::AudioState::Muted : media::RecordingStats::AudioState::Active)
                                                : media::RecordingStats::AudioState::SourceError;
                if (!r->audioEnabled) __android_log_print(ANDROID_LOG_WARN, "kiln", "camera: recording without audio (no microphone)");
            }
            // The camera renders into the encoder: the session with its surface too.
            d->recordWindow = r->input;
            Configure(camera, d->lastOutputs, d->settings);
            r->videoThread = std::thread([r] { r->VideoLoop(); });
            if (r->audioEnabled) r->audioThread = std::thread([r] { r->AudioLoop(); });
            g_Recorders[r->id] = r;
            return r->id;
        }

        void PauseRecording(std::uint64_t id, bool paused) {
            auto it = g_Recorders.find(id);
            if (it == g_Recorders.end()) return;
            Recorder& r = *it->second;
            {
                std::lock_guard lock(r.timeMutex);
                if (paused == r.paused) return;
                r.paused = paused;
                if (paused) r.pauseStartUs = r.NowUs();
                else r.pauses.push_back({ r.pauseStartUs, r.NowUs() });
            }
            if (!paused) {
                // A key frame to resume on.
                AMediaFormat* p = AMediaFormat_new();
                AMediaFormat_setInt32(p, AMEDIACODEC_KEY_REQUEST_SYNC_FRAME, 0);
                AMediaCodec_setParameters(r.video, p);
                AMediaFormat_delete(p);
            }
        }
        void MuteRecording(std::uint64_t id, bool muted) {
            auto it = g_Recorders.find(id);
            if (it == g_Recorders.end()) return;
            it->second->muted = muted;
            if (it->second->audioEnabled)
                it->second->audioState = muted ? media::RecordingStats::AudioState::Muted : media::RecordingStats::AudioState::Active;
        }

        // Ends the recording: the camera stops feeding the encoder, the streams end, the file is
        // finished on a thread of its own, then onFinished runs on the main thread.
        void StopRecording(std::uint64_t id, media::VideoRecordEvent::Error reason = media::VideoRecordEvent::Error::None) {
            auto it = g_Recorders.find(id);
            if (it == g_Recorders.end()) return;
            std::shared_ptr<Recorder> r = std::move(it->second);
            g_Recorders.erase(it);
            if (auto d = Find(r->camera); d && d->recordWindow == r->input) {
                d->recordWindow = nullptr;
                if (d->device) Configure(r->camera, d->lastOutputs, d->settings);
            }
            if (reason != media::VideoRecordEvent::Error::None) {
                r->error = reason;
                r->errorMessage = "the camera closed";
            }
            r->stopping = true;
            AMediaCodec_signalEndOfInputStream(r->video);
            std::thread([r] {
                if (r->videoThread.joinable()) r->videoThread.join();
                if (r->audioThread.joinable()) r->audioThread.join();
                if (r->mic) AAudioStream_requestStop(r->mic);
                bool kept;
                {
                    std::lock_guard lock(r->muxMutex);
                    kept = r->started && r->videoWritten;
                    if (r->started) AMediaMuxer_stop(r->muxer);
                }
                AMediaMuxer_delete(std::exchange(r->muxer, nullptr));
                close(std::exchange(r->fd, -1));
                auto error = r->error;
                std::string message = r->errorMessage;
                if (!kept) {
                    // ERROR_NO_VALID_DATA: nothing was recorded; the empty file goes.
                    unlink(r->config.file.c_str());
                    if (error == media::VideoRecordEvent::Error::None) {
                        error = media::VideoRecordEvent::Error::NoValidData;
                        message = "No valid data was recorded.";
                    }
                }
                r->main->PostFromAnyThread([r, error, message, kept]() mutable {
                    if (r->callbacks.onFinished) r->callbacks.onFinished(error, message, kept);
                    r.reset();   // the codecs go here, on the main thread
                });
            }).detach();
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
            // Its recordings end with it (their files are finished).
            std::vector<std::uint64_t> recordings;
            for (auto& [id, r] : g_Recorders)
                if (r->camera == handle) recordings.push_back(id);
            d->recordWindow = nullptr;   // the device is gone from the map: nothing is reconfigured
            for (auto id : recordings) StopRecording(id, media::VideoRecordEvent::Error::SourceInactive);
            // Closing the session and device here (synchronously) frees the windows; callbacks
            // still in flight find no device, or a closed one.
            d->Shutdown();
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
        h.startRecording = StartRecording;
        h.pauseRecording = PauseRecording;
        h.muteRecording = MuteRecording;
        h.stopRecording = [](std::uint64_t id) { StopRecording(id); };
        media::SetCameraHost(std::move(h));
    }

}
