// media::CameraHost on Android: the NDK's camera2 (ACameraManager), a TEMPLATE_PREVIEW repeating
// request into the surface's window.
#include "AndroidMedia.h"

#include "kiln/media/Camera.h"
#include "kiln/runtime/Task.h"

#include <android/log.h>
#include <android/native_window.h>
#include <camera/NdkCameraDevice.h>
#include <camera/NdkCameraManager.h>
#include <camera/NdkCameraMetadata.h>
#include <media/NdkImage.h>

#include <map>
#include <memory>
#include <mutex>

namespace kiln::android {

    namespace {
        ACameraManager* Manager() {
            static ACameraManager* m = ACameraManager_create();
            return m;
        }

        struct Preview {
            ACameraDevice* device = nullptr;
            ACaptureSessionOutputContainer* outputs = nullptr;
            ACaptureSessionOutput* output = nullptr;
            ACameraOutputTarget* target = nullptr;
            ACaptureRequest* request = nullptr;
            ACameraCaptureSession* session = nullptr;
            ANativeWindow* window = nullptr;
            kiln::Function<void(std::string)> onError;
            rt::Dispatcher* main = nullptr;
            ACameraDevice_StateCallbacks deviceCallbacks{};
            ACameraCaptureSession_stateCallbacks sessionCallbacks{};

            void Report(std::string message) {
                __android_log_print(ANDROID_LOG_ERROR, "kiln", "camera: %s", message.c_str());
                // The callbacks come on the camera's threads; the preview may be gone by the time
                // this runs, so it carries a copy of the handler.
                if (main && onError) main->PostFromAnyThread([f = onError, message] { f(message); });
            }
            ~Preview() {
                if (session) {
                    ACameraCaptureSession_stopRepeating(session);
                    ACameraCaptureSession_close(session);
                }
                if (request) ACaptureRequest_free(request);
                if (target) ACameraOutputTarget_free(target);
                if (outputs && output) ACaptureSessionOutputContainer_remove(outputs, output);
                if (output) ACaptureSessionOutput_free(output);
                if (outputs) ACaptureSessionOutputContainer_free(outputs);
                if (device) ACameraDevice_close(device);
                if (window) ANativeWindow_release(window);
            }
        };

        std::mutex g_Mutex;
        std::map<std::uint64_t, std::unique_ptr<Preview>> g_Previews;
        std::uint64_t g_Next = 1;

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
                if (ACameraMetadata_getConstEntry(meta, ACAMERA_LENS_FACING, &e) == ACAMERA_OK && e.count > 0)
                    info.facing = e.data.u8[0] == ACAMERA_LENS_FACING_FRONT ? media::LensFacing::Front
                                : e.data.u8[0] == ACAMERA_LENS_FACING_BACK ? media::LensFacing::Back : media::LensFacing::External;
                if (ACameraMetadata_getConstEntry(meta, ACAMERA_SENSOR_ORIENTATION, &e) == ACAMERA_OK && e.count > 0)
                    info.sensorOrientation = e.data.i32[0];
                if (ACameraMetadata_getConstEntry(meta, ACAMERA_SCALER_AVAILABLE_STREAM_CONFIGURATIONS, &e) == ACAMERA_OK) {
                    // (format, width, height, input) entries; outputs of the private format a
                    // surface receives.
                    for (std::uint32_t k = 0; k + 3 < e.count; k += 4) {
                        if (e.data.i32[k] == AIMAGE_FORMAT_PRIVATE && e.data.i32[k + 3] == ACAMERA_SCALER_AVAILABLE_STREAM_CONFIGURATIONS_OUTPUT)
                            info.previewSizes.push_back({ e.data.i32[k + 1], e.data.i32[k + 2] });
                    }
                }
                ACameraMetadata_free(meta);
                out.push_back(std::move(info));
            }
            ACameraManager_deleteCameraIdList(ids);
            return out;
        }

        std::uint64_t StartPreview(const std::string& id, ui::PlatformSurface surface, ui::IntSize size, kiln::Function<void(std::string)> onError) {
            auto p = std::make_unique<Preview>();
            p->onError = std::move(onError);
            p->main = &rt::Dispatcher::Main();
            p->window = static_cast<ANativeWindow*>(surface.nativeWindow);
            if (!p->window) return 0;
            ANativeWindow_acquire(p->window);
            __android_log_print(ANDROID_LOG_DEBUG, "kiln", "camera %s: preview %dx%d into a %dx%d window", id.c_str(), size.width, size.height,
                                ANativeWindow_getWidth(p->window), ANativeWindow_getHeight(p->window));

            p->deviceCallbacks.context = p.get();
            p->deviceCallbacks.onDisconnected = [](void* c, ACameraDevice*) { static_cast<Preview*>(c)->Report("the camera was disconnected"); };
            p->deviceCallbacks.onError = [](void* c, ACameraDevice*, int error) {
                static_cast<Preview*>(c)->Report("the camera failed (" + std::to_string(error) + ")");
            };
            camera_status_t s = ACameraManager_openCamera(Manager(), id.c_str(), &p->deviceCallbacks, &p->device);
            if (s != ACAMERA_OK) {
                p->Report(StatusText(s));
                return 0;
            }
            if (ACaptureSessionOutputContainer_create(&p->outputs) != ACAMERA_OK
                || ACaptureSessionOutput_create(p->window, &p->output) != ACAMERA_OK
                || ACaptureSessionOutputContainer_add(p->outputs, p->output) != ACAMERA_OK
                || ACameraOutputTarget_create(p->window, &p->target) != ACAMERA_OK
                || ACameraDevice_createCaptureRequest(p->device, TEMPLATE_PREVIEW, &p->request) != ACAMERA_OK
                || ACaptureRequest_addTarget(p->request, p->target) != ACAMERA_OK) {
                p->Report("cannot set up the preview");
                return 0;
            }
            p->sessionCallbacks.context = p.get();
            p->sessionCallbacks.onClosed = [](void*, ACameraCaptureSession*) {};
            p->sessionCallbacks.onReady = [](void*, ACameraCaptureSession*) {};
            p->sessionCallbacks.onActive = [](void*, ACameraCaptureSession*) {};
            s = ACameraDevice_createCaptureSession(p->device, p->outputs, &p->sessionCallbacks, &p->session);
            if (s == ACAMERA_OK) s = ACameraCaptureSession_setRepeatingRequest(p->session, nullptr, 1, &p->request, nullptr);
            if (s != ACAMERA_OK) {
                p->Report(StatusText(s));
                return 0;
            }
            std::lock_guard lock(g_Mutex);
            std::uint64_t handle = g_Next++;
            g_Previews[handle] = std::move(p);
            return handle;
        }

        void StopPreview(std::uint64_t handle) {
            std::unique_ptr<Preview> p;
            {
                std::lock_guard lock(g_Mutex);
                auto it = g_Previews.find(handle);
                if (it == g_Previews.end()) return;
                p = std::move(it->second);
                g_Previews.erase(it);
            }
            // Closing the session and device here (synchronously) frees the window.
        }
    }

    void InstallCamera() {
        media::CameraHost h;
        h.cameras = ListCameras;
        h.startPreview = StartPreview;
        h.stopPreview = StopPreview;
        media::SetCameraHost(std::move(h));
    }

}
