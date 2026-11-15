#include "kiln/media/Camera.h"

#include "kiln/Log.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <deque>
#include <functional>
#include <limits>

namespace kiln::media {

    namespace {
        CameraHost& Host() {
            static CameraHost h;
            return h;
        }
    }

    void SetCameraHost(CameraHost host) { Host() = std::move(host); }
    const CameraHost& GetCameraHost() { return Host(); }

    std::vector<CameraInfo> Cameras() { return Host().cameras ? Host().cameras() : std::vector<CameraInfo>{}; }

    std::optional<CameraInfo> FindCamera(LensFacing facing) {
        for (CameraInfo& c : Cameras()) if (c.facing == facing) return std::move(c);
        return std::nullopt;
    }

    int PreviewRotation(const CameraInfo& camera, int displayRotation) {
        return camera.facing == LensFacing::Front ? (camera.sensorOrientation + displayRotation) % 360
                                                  : (camera.sensorOrientation - displayRotation + 360) % 360;
    }

    std::array<float, 9> PreviewTransform(int rotation, bool mirror, float w, float h) {
        rotation = ((rotation % 360) + 360) % 360;
        // Quarter turns only: exact cosines and sines.
        const float c = rotation == 0 ? 1.0f : rotation == 180 ? -1.0f : 0.0f;
        const float s = rotation == 90 ? 1.0f : rotation == 270 ? -1.0f : 0.0f;
        // The stretch undone: a quarter-turned frame swaps the element's aspect ratio.
        const float sx = rotation % 180 != 0 ? h / w : 1.0f, sy = rotation % 180 != 0 ? w / h : 1.0f;
        float a = c * sx, b = -s * sy, d = s * sx, e = c * sy;
        if (mirror) {
            a = -a;
            b = -b;
        }
        // About the element's centre.
        const float tx = w / 2 - (a * w / 2 + b * h / 2), ty = h / 2 - (d * w / 2 + e * h / 2);
        return { a, b, tx, d, e, ty, 0, 0, 1 };
    }

    namespace {
        // The size with the aspect ratio nearest `aspect`, the largest of those, within `limit`.
        ui::IntSize Choose(const std::vector<ui::IntSize>& sizes, float aspect, int maxLong, int maxShort) {
            ui::IntSize best{};
            float bestAspectError = 1e9f;
            for (const ui::IntSize& s : sizes) {
                const int longSide = std::max(s.width, s.height), shortSide = std::min(s.width, s.height);
                if (s.width <= 0 || s.height <= 0 || longSide > maxLong || shortSide > maxShort) continue;
                const float error = aspect > 0 ? std::abs(std::log(float(s.width) / float(s.height) / aspect)) : 0;
                const bool closer = error < bestAspectError - 1e-3f;
                const bool asClose = std::abs(error - bestAspectError) <= 1e-3f;
                if (closer || (asClose && std::int64_t(s.width) * s.height > std::int64_t(best.width) * best.height)) {
                    best = s;
                    bestAspectError = error;
                }
            }
            return best;
        }
    }

    ui::IntSize ChoosePreviewSize(const std::vector<ui::IntSize>& sizes, float aspect) { return Choose(sizes, aspect, 1920, 1080); }
    ui::IntSize ChooseCaptureSize(const std::vector<ui::IntSize>& sizes, float aspect) {
        return Choose(sizes, aspect, std::numeric_limits<int>::max(), std::numeric_limits<int>::max());
    }

    // --- ZoomMath ---------------------------------------------------------------------------------

    namespace {
        bool NearZero(float v) { return std::abs(v) < 2.0f * std::nextafter(std::abs(v), INFINITY) - 2.0f * std::abs(v) + 1e-30f; }
        bool FloatsEqual(float a, float b) {
            // ZoomMath.areFloatsEqual: |a - b| < 2 ulp(|a - b|), i.e. a == b in practice.
            const float d = std::abs(a - b);
            return d == 0 || d < 2.0f * (std::nextafter(d, INFINITY) - d);
        }
    }

    float LinearZoomFromZoomRatio(float zoomRatio, float minZoomRatio, float maxZoomRatio) {
        if (FloatsEqual(minZoomRatio, maxZoomRatio)) return 0;   // zoom unsupported
        if (zoomRatio == 0 || NearZero(zoomRatio)) return 0;
        if (FloatsEqual(zoomRatio, maxZoomRatio)) return 1;
        if (FloatsEqual(zoomRatio, minZoomRatio)) return 0;
        // The share of the crop width given up, relative to the range.
        const float crop = 1.0f / zoomRatio, cropMax = 1.0f / maxZoomRatio, cropMin = 1.0f / minZoomRatio;
        return std::clamp((cropMin - crop) / (cropMin - cropMax), 0.0f, 1.0f);
    }

    float ZoomRatioFromLinearZoom(float linearZoom, float minZoomRatio, float maxZoomRatio) {
        if (FloatsEqual(linearZoom, 1)) return maxZoomRatio;
        if (FloatsEqual(linearZoom, 0)) return minZoomRatio;
        const float cropMax = 1.0f / maxZoomRatio, cropMin = 1.0f / minZoomRatio;
        const float crop = cropMin - (cropMin - cropMax) * linearZoom;
        return std::clamp(1.0f / crop, minZoomRatio, maxZoomRatio);
    }

    // --- metering ---------------------------------------------------------------------------------

    FocusMeteringAction FocusMeteringAction::Of(MeteringPoint point, int modes) {
        FocusMeteringAction a;
        a.AddPoint(point, modes);
        return a;
    }
    FocusMeteringAction& FocusMeteringAction::AddPoint(MeteringPoint point, int modes) {
        if (modes & kMeteringAf) afPoints.push_back(point);
        if (modes & kMeteringAe) aePoints.push_back(point);
        if (modes & kMeteringAwb) awbPoints.push_back(point);
        return *this;
    }

    std::vector<MeteringRectangle> MeteringRegionsFromMeteringPoints(const std::vector<MeteringPoint>& points, int maxRegions,
                                                                     ui::IntRect crop, float defaultAspectRatio) {
        std::vector<MeteringRectangle> out;
        if (points.empty() || maxRegions == 0 || crop.Empty()) return out;
        const float cropAspect = float(crop.Width()) / float(crop.Height());
        for (const MeteringPoint& p : points) {
            if (int(out.size()) >= maxRegions) break;
            if (!(p.x >= 0 && p.x <= 1 && p.y >= 0 && p.y <= 1)) continue;
            // getFovAdjustedPoint: the point's field of view letterboxed into the crop region's.
            float x = p.x, y = p.y;
            const float fov = p.surfaceAspectRatio.value_or(defaultAspectRatio);
            if (std::abs(fov - cropAspect) > 1e-6f) {
                if (fov > cropAspect) {
                    const float vertical = fov / cropAspect;
                    y = ((vertical - 1) / 2 + y) * (1 / vertical);
                } else {
                    const float horizontal = cropAspect / fov;
                    x = ((horizontal - 1) / 2 + x) * (1 / horizontal);
                }
            }
            // getMeteringRect: centred there, the point's size of the crop region, inside it.
            const int cx = int(float(crop.left) + x * float(crop.Width())), cy = int(float(crop.top) + y * float(crop.Height()));
            const int w = int(p.size * float(crop.Width())), h = int(p.size * float(crop.Height()));
            ui::IntRect r{ cx - w / 2, cy - h / 2, cx + w / 2, cy + h / 2 };
            r.left = std::clamp(r.left, crop.left, crop.right);
            r.right = std::clamp(r.right, crop.left, crop.right);
            r.top = std::clamp(r.top, crop.top, crop.bottom);
            r.bottom = std::clamp(r.bottom, crop.top, crop.bottom);
            out.push_back({ r, 1000 });
        }
        return out;
    }

    MeteringPoint PreviewMeteringPoint(ui::Offset tap, ui::IntSize element, int rotation, bool mirror, ui::IntSize previewSize) {
        MeteringPoint p;
        if (element.width <= 0 || element.height <= 0) return p;
        // Undo the preview's transform: the tap in the stretched sensor frame.
        const auto m = PreviewTransform(rotation, mirror, float(element.width), float(element.height));
        const float det = m[0] * m[4] - m[1] * m[3];
        if (det == 0) return p;
        const float dx = tap.x - m[2], dy = tap.y - m[5];
        const float sx = (m[4] * dx - m[1] * dy) / det, sy = (-m[3] * dx + m[0] * dy) / det;
        p.x = sx / float(element.width);
        p.y = sy / float(element.height);
        if (previewSize.width > 0 && previewSize.height > 0) p.surfaceAspectRatio = float(previewSize.width) / float(previewSize.height);
        return p;
    }

    // --- Camera -----------------------------------------------------------------------------------

    namespace {
        CameraError Canceled(std::string m) { return { CameraError::Kind::OperationCanceled, std::move(m) }; }
        CameraError Argument(std::string m) { return { CameraError::Kind::IllegalArgument, std::move(m) }; }
        bool Has(const std::vector<int>& v, int x) { return std::find(v.begin(), v.end(), x) != v.end(); }
        constexpr int kAfModeOff = 0, kAfModeAuto = 1, kAfModeContinuousPicture = 4;
        constexpr int kAeModeOff = 0, kAeModeOn = 1, kAeModeOnAutoFlash = 2, kAeModeOnAlwaysFlash = 3;
        // CapturePipeline's 3A time limits.
        constexpr std::int64_t kCheck3aTimeoutMs = 1000, kCheck3aWithFlashTimeoutMs = 5000, kAutoFocusTimeoutMs = 5000;

        rt::Job After(std::int64_t ms, kiln::Function<void()> fn) {
            return rt::Launch(rt::Dispatcher::Main(), [](std::int64_t delay, kiln::Function<void()> f) -> rt::Task<> {
                co_await rt::Delay(delay);
                f();
            }(ms, std::move(fn)));
        }
        std::string Format(const char* fmt, float a, float b, float c) {
            char buf[160];
            std::snprintf(buf, sizeof buf, fmt, double(a), double(b), double(c));
            return buf;
        }
    }

    struct Camera::Impl : std::enable_shared_from_this<Impl> {
        CameraInfo info;
        CameraOptions options;
        std::uint64_t handle = 0;
        bool open = false;
        rt::MutableStateFlow<CameraState> state{ CameraState{} };
        CameraOutputs outputs;
        FlashMode flashMode = FlashMode::Off;

        // The repeating request's parts (State3AControl, ZoomControl, EvCompControl, TorchControl).
        float zoomRatio = 1;
        bool torch = false;
        int evIndex = 0;
        std::optional<int> preferredAfMode;   // AUTO while focus is locked
        std::optional<std::vector<MeteringRectangle>> afRegions, aeRegions, awbRegions;
        bool aeLock = false, awbLock = false;
        CameraRequest applied;

        // Operations completing with the next frame that carries their request.
        CameraDone zoomDone, torchDone, evDone;
        // The focus and metering in progress.
        struct Focus {
            kiln::Function<void(std::optional<FocusMeteringResult>, std::optional<CameraError>)> done;
            bool triggerAf = false, lockAe = false, lockAwb = false, aeAwbLocked = false;
            rt::Job timeout, autoCancel;
        };
        std::shared_ptr<Focus> focus;
        // Frame waiters: called with each result until they return true.
        std::vector<kiln::Function<bool(const CameraFrameResult&)>> waiters;
        std::optional<CameraFrameResult> lastResult;

        // Pictures, one at a time (TakePictureManager).
        struct Picture {
            OutputFileOptions output;
            kiln::Function<void(ImageCaptureResult)> done;
            int displayRotation = 0;
        };
        std::deque<Picture> pictures;
        bool capturing = false;
        // The picture camera2 has: answered once, by its JPEG or by the camera closing.
        std::shared_ptr<kiln::Function<void(ImageCaptureResult)>> inflight;

        CameraState Base() const { return state.Value(); }
        void SetState(const std::function<void(CameraState&)>& f) {
            CameraState s = state.Value();
            f(s);
            state.SetValue(std::move(s));
        }

        ZoomState Zoom(float ratio) const {
            return { ratio, info.minZoomRatio, info.maxZoomRatio, LinearZoomFromZoomRatio(ratio, info.minZoomRatio, info.maxZoomRatio) };
        }
        // ZoomCompat.getCropSensorRegion: the active array (the zoom ratio's regions are relative
        // to the zoomed field of view), or the crop region zooming.
        ui::IntRect CropRegion() const {
            const ui::IntRect a = info.activeArray;
            if (info.zoomRatioControl || zoomRatio <= 1) return a;
            const int w = int(float(a.Width()) / zoomRatio), h = int(float(a.Height()) / zoomRatio);
            const int l = a.left + (a.Width() - w) / 2, t = a.top + (a.Height() - h) / 2;
            return { l, t, l + w, t + h };
        }
        int SupportedAeMode(int preferred) const {
            // FocusMeteringControl.getSupportedAeMode / State3AControl.getSupportedAeMode.
            if (info.aeModes.empty()) return kAeModeOn;
            if (Has(info.aeModes, preferred)) return preferred;
            return Has(info.aeModes, kAeModeOn) ? kAeModeOn : kAeModeOff;
        }
        int SupportedAfMode(int preferred) const {
            if (info.afModes.empty()) return kAfModeOff;
            if (Has(info.afModes, preferred)) return preferred;
            if (Has(info.afModes, kAfModeContinuousPicture)) return kAfModeContinuousPicture;
            return Has(info.afModes, kAfModeAuto) ? kAfModeAuto : kAfModeOff;
        }
        CameraRequest Request() const {
            CameraRequest r;
            if (info.zoomRatioControl) r.zoomRatio = zoomRatio;
            else if (zoomRatio != 1) r.cropRegion = CropRegion();
            r.torch = torch;
            r.aeCompensation = evIndex;
            r.afMode = SupportedAfMode(preferredAfMode.value_or(kAfModeContinuousPicture));
            // State3AControl: the torch keeps AE on; otherwise the flash mode chooses it.
            const int ae = torch ? kAeModeOn
                         : flashMode == FlashMode::On ? kAeModeOnAlwaysFlash
                         : flashMode == FlashMode::Auto ? kAeModeOnAutoFlash : kAeModeOn;
            r.aeMode = SupportedAeMode(ae);
            r.afRegions = afRegions;
            r.aeRegions = aeRegions;
            r.awbRegions = awbRegions;
            r.aeLock = aeLock;
            r.awbLock = awbLock;
            return r;
        }
        void Apply() {
            if (!open) return;
            CameraRequest r = Request();
            if (r == applied) return;
            applied = r;
            if (Host().setRequest) Host().setRequest(handle, r);
        }
        // Completes `done` with the next frame (the request has reached the camera by then).
        void OnNextFrame(kiln::Function<void()> f) {
            waiters.push_back([f = std::move(f)](const CameraFrameResult&) mutable {
                f();
                return true;
            });
        }

        void Configure() {
            if (!open || !Host().configure) return;
            applied = Request();
            if (!Host().configure(handle, outputs, applied))
                SetState([](CameraState& s) {
                    s.error = CameraState::Error::StreamConfig;
                    s.errorMessage = "the camera cannot stream to these outputs";
                });
        }

        void OnOpened() {
            open = true;
            SetState([](CameraState& s) {
                s.type = CameraState::Type::Open;
                s.error = CameraState::Error::None;
                s.errorMessage.clear();
            });
            Configure();
        }
        void OnError(CameraState::Error e, std::string message) {
            const bool fatal = e != CameraState::Error::OtherRecoverable;
            if (fatal) Lost();
            SetState([&](CameraState& s) {
                s.error = e;
                s.errorMessage = std::move(message);
                if (fatal) s.type = CameraState::Type::Closed;
            });
        }
        // The camera is gone: pending operations fail as CameraX's do when it closes.
        void Lost() {
            open = false;
            waiters.clear();
            if (zoomDone) std::exchange(zoomDone, {})(Canceled("Camera is not active."));
            if (torchDone) std::exchange(torchDone, {})(Canceled("Camera is not active."));
            if (evDone) std::exchange(evDone, {})(Canceled("Camera is not active."));
            if (focus) {
                auto f = std::exchange(focus, {});
                f->timeout.Cancel();
                f->autoCancel.Cancel();
                if (f->done) f->done(std::nullopt, Canceled("Camera is not active."));
            }
            auto pending = std::move(pictures);
            pictures.clear();
            capturing = false;
            if (inflight && *inflight) std::exchange(*inflight, {})({ .error = ImageCaptureResult::Error::CameraClosed, .message = "Camera is closed." });
            inflight.reset();
            for (auto& p : pending)
                if (p.done) p.done({ .error = ImageCaptureResult::Error::CameraClosed, .message = "Camera is closed." });
        }
        void OnResult(const CameraFrameResult& r) {
            lastResult = r;
            // A waiter may add waiters: run a snapshot, keep those not done.
            auto run = std::move(waiters);
            waiters.clear();
            std::vector<kiln::Function<bool(const CameraFrameResult&)>> keep;
            for (auto& w : run)
                if (!w(r)) keep.push_back(std::move(w));
            for (auto& w : waiters) keep.push_back(std::move(w));
            waiters = std::move(keep);
        }

        // --- focus and metering (FocusMeteringControl) -------------------------------------------

        float DefaultAspectRatio() const {
            if (outputs.previewSize.width > 0 && outputs.previewSize.height > 0)
                return float(outputs.previewSize.width) / float(outputs.previewSize.height);
            const ui::IntRect c = CropRegion();
            return c.Height() > 0 ? float(c.Width()) / float(c.Height()) : 1.0f;
        }
        bool SupportsAfTrigger() const { return Has(info.afModes, kAfModeAuto) || Has(info.afModes, 2 /* MACRO */); }

        static std::vector<MeteringRectangle> DefaultRegions() { return { MeteringRectangle{ {}, 0 } }; }

        void CancelFocusNow(bool resetRegions = true) {
            if (focus) {
                focus->timeout.Cancel();
                focus->autoCancel.Cancel();
            }
            // cancel3ALocksAndResetMode: AF cancelled, regions back to the default, AE / AWB unlocked.
            preferredAfMode.reset();
            if (resetRegions) {
                afRegions = info.maxAfRegions > 0 ? std::optional(DefaultRegions()) : std::nullopt;
                aeRegions = info.maxAeRegions > 0 ? std::optional(DefaultRegions()) : std::nullopt;
                awbRegions = info.maxAwbRegions > 0 ? std::optional(DefaultRegions()) : std::nullopt;
            }
            aeLock = awbLock = false;
            if (open && Host().trigger) Host().trigger(handle, Request(), CameraTrigger::AfCancel);
            Apply();
        }
    };

    Camera::Camera(std::string cameraId, CameraOptions options) : m_Impl(std::make_shared<Impl>()) {
        Impl& m = *m_Impl;
        m.options = options;
        if (options.imageCapture) m.flashMode = options.imageCapture->flashMode;
        for (auto& c : Cameras())
            if (c.id == cameraId) m.info = c;
        if (m.info.id.empty()) m.info.id = cameraId;
        m.SetState([&](CameraState& s) {
            s.zoom = m.Zoom(1);
            s.exposure = { 0, m.info.exposureCompensationMin, m.info.exposureCompensationMax, m.info.exposureCompensationStepNumerator,
                           m.info.exposureCompensationStepDenominator,
                           m.info.exposureCompensationMin != 0 || m.info.exposureCompensationMax != 0 };
        });
        if (!Host().open) {
            m.SetState([](CameraState& s) {
                s.type = CameraState::Type::Closed;
                s.error = CameraState::Error::CameraFatal;
                s.errorMessage = "no camera on this platform";
            });
            return;
        }
        m.SetState([](CameraState& s) { s.type = CameraState::Type::Opening; });
        std::weak_ptr<Impl> weak = m_Impl;
        m.handle = Host().open(m.info.id, {
            .onOpened = [weak] { if (auto i = weak.lock()) i->OnOpened(); },
            .onError = [weak](CameraState::Error e, std::string message) { if (auto i = weak.lock()) i->OnError(e, std::move(message)); },
            .onResult = [weak](const CameraFrameResult& r) { if (auto i = weak.lock()) i->OnResult(r); },
        });
        if (!m.handle) m.OnError(CameraState::Error::CameraFatal, "the camera cannot be opened");
        const ImageCaptureOptions* ic = options.imageCapture ? &*options.imageCapture : nullptr;
        if (ic) m.outputs.jpegSize = ChooseCaptureSize(m.info.jpegSizes, ic->targetAspectRatio > 0 ? ic->targetAspectRatio : 4.0f / 3.0f);
    }

    Camera::~Camera() {
        Impl& m = *m_Impl;
        if (m.handle && Host().close) Host().close(m.handle);
        m.Lost();
    }

    const CameraInfo& Camera::Info() const { return m_Impl->info; }
    const CameraState& Camera::State() const { return m_Impl->state.Value(); }
    rt::MutableStateFlow<CameraState>& Camera::StateFlow() { return m_Impl->state; }

    // ZoomControl.setZoomRatio / setLinearZoom: the state changes at once; the operation completes
    // when the camera has the value, or is cancelled by the next one.
    void Camera::SetZoomRatio(float ratio, CameraDone done) {
        Impl& m = *m_Impl;
        if (ratio > m.info.maxZoomRatio || ratio < m.info.minZoomRatio) {
            if (done) done(Argument(Format("Requested zoomRatio %g is not within valid range [%g, %g]", ratio, m.info.minZoomRatio, m.info.maxZoomRatio)));
            return;
        }
        if (m.zoomDone) std::exchange(m.zoomDone, {})(Canceled("Cancelled due to another zoom value being set."));
        m.zoomRatio = ratio;
        m.SetState([&](CameraState& s) { s.zoom = m.Zoom(ratio); });
        if (!m.open) {
            if (done) done(Canceled("Camera is not active."));
            return;
        }
        m.zoomDone = std::move(done);
        m.Apply();
        std::weak_ptr<Impl> weak = m_Impl;
        m.OnNextFrame([weak] {
            if (auto i = weak.lock(); i && i->zoomDone) std::exchange(i->zoomDone, {})(std::nullopt);
        });
    }
    void Camera::SetLinearZoom(float linearZoom, CameraDone done) {
        Impl& m = *m_Impl;
        if (linearZoom > 1 || linearZoom < 0) {
            char buf[96];
            std::snprintf(buf, sizeof buf, "Requested linearZoom %g is not within valid range [0, 1]", double(linearZoom));
            if (done) done(Argument(buf));
            return;
        }
        const float ratio = ZoomRatioFromLinearZoom(linearZoom, m.info.minZoomRatio, m.info.maxZoomRatio);
        SetZoomRatio(std::clamp(ratio, m.info.minZoomRatio, m.info.maxZoomRatio), std::move(done));
        // The linear value asked for, not the one recomputed from the ratio (ZoomValue.LinearZoom).
        if (m.state.Value().zoom.zoomRatio == ratio) m.SetState([&](CameraState& s) { s.zoom.linearZoom = linearZoom; });
    }

    // TorchControl.setTorchAsync.
    void Camera::EnableTorch(bool on, CameraDone done) {
        Impl& m = *m_Impl;
        if (!m.info.hasFlashUnit) {
            if (done) done(CameraError{ CameraError::Kind::IllegalState, "No flash unit" });
            return;
        }
        if (!m.open) {
            if (done) done(Canceled("Camera is not active."));
            return;
        }
        if (m.torchDone) std::exchange(m.torchDone, {})(Canceled("There is a new enableTorch being set"));
        m.torch = on;
        m.SetState([&](CameraState& s) { s.torch = on ? TorchState::On : TorchState::Off; });
        m.torchDone = std::move(done);
        m.Apply();
        std::weak_ptr<Impl> weak = m_Impl;
        m.OnNextFrame([weak] {
            if (auto i = weak.lock(); i && i->torchDone) std::exchange(i->torchDone, {})(std::nullopt);
        });
    }

    // EvCompControl.updateAsync.
    void Camera::SetExposureCompensationIndex(int index, CameraDone done) {
        Impl& m = *m_Impl;
        const ExposureState& e = m.state.Value().exposure;
        if (!e.isExposureCompensationSupported) {
            if (done) done(Argument("ExposureCompensation is not supported"));
            return;
        }
        if (index < e.rangeLower || index > e.rangeUpper) {
            if (done)
                done(Argument("Requested ExposureCompensation " + std::to_string(index) + " is not within valid range [" +
                              std::to_string(e.rangeUpper) + " .. " + std::to_string(e.rangeLower) + "]"));
            return;
        }
        if (!m.open) {
            if (done) done(Canceled("Camera is not active."));
            return;
        }
        if (m.evDone) std::exchange(m.evDone, {})(Canceled("Cancelled by another setExposureCompensationIndex()"));
        m.evIndex = index;
        m.SetState([&](CameraState& s) { s.exposure.exposureCompensationIndex = index; });
        m.evDone = std::move(done);
        m.Apply();
        std::weak_ptr<Impl> weak = m_Impl;
        m.OnNextFrame([weak] {
            if (auto i = weak.lock(); i && i->evDone) std::exchange(i->evDone, {})(std::nullopt);
        });
    }

    bool Camera::IsFocusMeteringSupported(const FocusMeteringAction& a) const {
        const Impl& m = *m_Impl;
        const ui::IntRect crop = m.CropRegion();
        const float aspect = m.DefaultAspectRatio();
        return !MeteringRegionsFromMeteringPoints(a.afPoints, m.info.maxAfRegions, crop, aspect).empty()
            || !MeteringRegionsFromMeteringPoints(a.aePoints, m.info.maxAeRegions, crop, aspect).empty()
            || !MeteringRegionsFromMeteringPoints(a.awbPoints, m.info.maxAwbRegions, crop, aspect).empty();
    }

    // FocusMeteringControl.startFocusAndMetering.
    void Camera::StartFocusAndMetering(FocusMeteringAction action,
                                       kiln::Function<void(std::optional<FocusMeteringResult>, std::optional<CameraError>)> done) {
        Impl& m = *m_Impl;
        if (!m.open) {
            if (done) done(std::nullopt, Canceled("Camera is not active."));
            return;
        }
        if (m.focus) {
            auto old = std::exchange(m.focus, {});
            old->timeout.Cancel();
            old->autoCancel.Cancel();
            if (old->done) old->done(std::nullopt, Canceled("Cancelled by another startFocusAndMetering()"));
        }
        const ui::IntRect crop = m.CropRegion();
        const float aspect = m.DefaultAspectRatio();
        auto ae = MeteringRegionsFromMeteringPoints(action.aePoints, m.info.maxAeRegions, crop, aspect);
        auto af = MeteringRegionsFromMeteringPoints(action.afPoints, m.info.maxAfRegions, crop, aspect);
        auto awb = MeteringRegionsFromMeteringPoints(action.awbPoints, m.info.maxAwbRegions, crop, aspect);
        if (ae.empty() && af.empty() && awb.empty()) {
            if (done) done(std::nullopt, Argument("None of the specified AF/AE/AWB MeteringPoints is supported on this camera."));
            return;
        }
        auto f = std::make_shared<Impl::Focus>();
        f->done = std::move(done);
        f->triggerAf = !af.empty();
        f->lockAe = m.info.maxAeRegions > 0 && m.info.aeLockAvailable && !ae.empty() && (action.lockingMode & kMeteringAe);
        f->lockAwb = m.info.maxAwbRegions > 0 && m.info.awbLockAvailable && !awb.empty() && (action.lockingMode & kMeteringAwb);
        const bool lockAf = m.info.maxAfRegions > 0 && m.SupportsAfTrigger() && !af.empty() && (action.lockingMode & kMeteringAf);
        m.focus = f;
        if (lockAf) m.preferredAfMode = kAfModeAuto;
        m.aeRegions = m.info.maxAeRegions > 0 ? std::optional(ae.empty() ? Impl::DefaultRegions() : ae) : std::nullopt;
        m.afRegions = m.info.maxAfRegions > 0 ? std::optional(af.empty() ? Impl::DefaultRegions() : af) : std::nullopt;
        m.awbRegions = m.info.maxAwbRegions > 0 ? std::optional(awb.empty() ? Impl::DefaultRegions() : awb) : std::nullopt;
        m.aeLock = m.awbLock = false;
        m.Apply();

        std::weak_ptr<Impl> weak = m_Impl;
        std::weak_ptr<Impl::Focus> weakFocus = f;
        auto finish = [weak, weakFocus](std::optional<FocusMeteringResult> r) {
            auto i = weak.lock();
            auto fo = weakFocus.lock();
            if (!i || !fo || i->focus != fo) return;
            fo->timeout.Cancel();
            auto d = std::exchange(fo->done, {});
            if (d) d(r, std::nullopt);
        };
        // toFocusMeteringResult: focused when AF was triggered and locked in focus (or the camera
        // has no AF to trigger).
        const bool afAuto = Has(m.info.afModes, kAfModeAuto);
        auto result = [triggerAf = f->triggerAf, afAuto](const CameraFrameResult& r) {
            if (!triggerAf) return FocusMeteringResult{ false };
            if (!afAuto) return FocusMeteringResult{ true };
            return FocusMeteringResult{ r.afState == kAfStateFocusedLocked };
        };
        const bool lockSomething = lockAf || f->lockAe || f->lockAwb;
        if (!lockSomething) {
            // Only the regions change: the result is the next frame's.
            m.waiters.push_back([finish, result](const CameraFrameResult& r) {
                finish(result(r));
                return true;
            });
        } else {
            const std::int64_t focusTimeout = action.autoCancelDurationMs > 0 && action.autoCancelDurationMs < kAutoFocusTimeoutMs
                                                  ? action.autoCancelDurationMs : kAutoFocusTimeoutMs;
            // lock3A: AE / AWB converge (then lock), AF triggered and locked.
            if (lockAf && Host().trigger) Host().trigger(m.handle, m.Request(), CameraTrigger::AfStart);
            const bool lockAe = f->lockAe, lockAwb = f->lockAwb;
            m.waiters.push_back([weak, weakFocus, finish, result, lockAf, lockAe, lockAwb](const CameraFrameResult& r) {
                auto i = weak.lock();
                auto fo = weakFocus.lock();
                if (!i || !fo || i->focus != fo) return true;
                if ((lockAe || lockAwb) && !fo->aeAwbLocked) {
                    const bool aeOk = !lockAe || r.aeState == kAeStateConverged || r.aeState == kAeStateFlashRequired || r.aeState == kAeStateLocked;
                    const bool awbOk = !lockAwb || r.awbState == kAwbStateConverged || r.awbState == kAwbStateLocked;
                    if (!aeOk || !awbOk) return false;
                    fo->aeAwbLocked = true;
                    i->aeLock = lockAe;
                    i->awbLock = lockAwb;
                    i->Apply();
                    if (!lockAf) {
                        finish(result(r));
                        return true;
                    }
                }
                if (lockAf && r.afState != kAfStateFocusedLocked && r.afState != kAfStateNotFocusedLocked) return false;
                if ((lockAe || lockAwb) && !fo->aeAwbLocked) return false;
                finish(result(r));
                return true;
            });
            // TIME_LIMIT_REACHED: not focused.
            f->timeout = After(focusTimeout, [finish] { finish(FocusMeteringResult{ false }); });
        }
        if (action.autoCancelDurationMs > 0) {
            f->autoCancel = After(action.autoCancelDurationMs, [weak, weakFocus] {
                auto i = weak.lock();
                auto fo = weakFocus.lock();
                if (!i || !fo || i->focus != fo) return;
                i->focus.reset();
                if (fo->done) std::exchange(fo->done, {})(std::nullopt, Canceled("Cancelled by cancelFocusAndMetering()"));
                i->CancelFocusNow();
            });
        }
    }

    void Camera::CancelFocusAndMetering(CameraDone done) {
        Impl& m = *m_Impl;
        if (!m.open) {
            if (done) done(Canceled("Camera is not active."));
            return;
        }
        if (m.focus) {
            auto f = std::exchange(m.focus, {});
            f->timeout.Cancel();
            f->autoCancel.Cancel();
            if (f->done) f->done(std::nullopt, Canceled("Cancelled by cancelFocusAndMetering()"));
        }
        m.CancelFocusNow();
        if (done) m.OnNextFrame([d = std::move(done)]() mutable { d(std::nullopt); });
    }

    // --- ImageCapture -----------------------------------------------------------------------------

    void Camera::SetFlashMode(FlashMode mode) {
        m_Impl->flashMode = mode;
        m_Impl->Apply();
    }
    FlashMode Camera::GetFlashMode() const { return m_Impl->flashMode; }

    namespace {
        void RunNextPicture(const std::shared_ptr<Camera::Impl>& m);

        // CapturePipeline: 3A for the capture (AE precapture, AF for maximum quality) within its
        // time limit, the capture, then 3A unlocked.
        void Capture(const std::shared_ptr<Camera::Impl>& self, Camera::Impl::Picture picture) {
            Camera::Impl& m = *self;
            const ImageCaptureOptions ic = *m.options.imageCapture;
            const bool maxQuality = ic.captureMode == CaptureMode::MaximizeQuality;
            // isPhysicalFlashRequired: on, or auto with AE asking for it.
            const bool flash = m.info.hasFlashUnit && !m.torch
                            && (m.flashMode == FlashMode::On
                                || (m.flashMode == FlashMode::Auto && m.lastResult && m.lastResult->aeState == kAeStateFlashRequired));
            const bool precapture = m.info.hasFlashUnit && (flash || maxQuality);
            const bool afTrigger = maxQuality && m.SupportsAfTrigger();
            const std::int64_t limit = flash ? kCheck3aWithFlashTimeoutMs : kCheck3aTimeoutMs;
            const int jpegQuality = ic.jpegQuality > 0 ? ic.jpegQuality : maxQuality ? 100 : 95;
            const int orientation = PreviewRotation(m.info, picture.displayRotation);
            std::weak_ptr<Camera::Impl> weak = self;

            auto answer = std::make_shared<kiln::Function<void(ImageCaptureResult)>>(std::move(picture.done));
            m.inflight = answer;
            auto shoot = std::make_shared<kiln::Function<void()>>();
            *shoot = [weak, picture = std::move(picture), answer, precapture, afTrigger, jpegQuality, orientation]() mutable {
                auto i = weak.lock();
                if (!i || !i->open || !Host().capture) return;
                Host().capture(i->handle, i->Request(), jpegQuality, orientation,
                               [weak, picture = std::move(picture), answer, precapture, afTrigger, orientation](std::vector<std::uint8_t> jpeg, std::string error) mutable {
                    auto i = weak.lock();
                    if (!i || !*answer) return;   // answered already (the camera closed)
                    // unlock3APostCapture.
                    if (i->open && Host().trigger) {
                        if (precapture) Host().trigger(i->handle, i->Request(), CameraTrigger::AePrecaptureCancel);
                        if (afTrigger) Host().trigger(i->handle, i->Request(), CameraTrigger::AfCancel);
                    }
                    ImageCaptureResult r;
                    r.rotationDegrees = orientation;
                    if (jpeg.empty()) {
                        r.error = ImageCaptureResult::Error::CaptureFailed;
                        r.message = error.empty() ? "Capture failed." : error;
                    } else if (!picture.output.file.empty()) {
                        // ImageSaver: written whole, or not at all.
                        const std::string temp = picture.output.file + ".tmp";
                        std::FILE* f = std::fopen(temp.c_str(), "wb");
                        const bool ok = f && std::fwrite(jpeg.data(), 1, jpeg.size(), f) == jpeg.size();
                        if (f) std::fclose(f);
                        if (ok && std::rename(temp.c_str(), picture.output.file.c_str()) == 0) {
                            r.savedUri = picture.output.file;
                        } else {
                            std::remove(temp.c_str());
                            r.error = ImageCaptureResult::Error::FileIo;
                            r.message = "Failed to write to the file: " + picture.output.file;
                        }
                    } else {
                        r.jpeg = std::move(jpeg);
                    }
                    i->capturing = false;
                    i->inflight.reset();
                    std::exchange(*answer, {})(std::move(r));
                    RunNextPicture(i);
                });
            };

            if (!precapture && !afTrigger) {
                (*shoot)();
                return;
            }
            // lock3AForCapture: triggers, then wait for AE (and AF, AWB for maximum quality) to settle.
            if (Host().trigger) {
                if (precapture) Host().trigger(m.handle, m.Request(), CameraTrigger::AePrecaptureStart);
                if (afTrigger) Host().trigger(m.handle, m.Request(), CameraTrigger::AfStart);
            }
            auto fired = std::make_shared<bool>(false);
            auto go = [shoot, fired] {
                if (*fired) return;
                *fired = true;
                (*shoot)();
            };
            auto timeout = std::make_shared<rt::Job>(After(limit + (afTrigger && !precapture ? kCheck3aTimeoutMs : 0), go));
            const bool waitAwb = maxQuality;
            m.waiters.push_back([go, fired, timeout, precapture, afTrigger, waitAwb](const CameraFrameResult& r) {
                if (*fired) return true;
                const bool aeOk = !precapture || r.aeState == kAeStateConverged || r.aeState == kAeStateFlashRequired || r.aeState == kAeStateLocked;
                const bool afOk = !afTrigger || r.afState == kAfStateFocusedLocked || r.afState == kAfStateNotFocusedLocked;
                const bool awbOk = !waitAwb || !precapture || r.awbState == kAwbStateConverged || r.awbState == kAwbStateLocked;
                if (!aeOk || !afOk || !awbOk) return false;
                timeout->Cancel();
                go();
                return true;
            });
        }

        void RunNextPicture(const std::shared_ptr<Camera::Impl>& m) {
            if (m->capturing || m->pictures.empty()) return;
            m->capturing = true;
            Camera::Impl::Picture p = std::move(m->pictures.front());
            m->pictures.pop_front();
            Capture(m, std::move(p));
        }
    }

    void Camera::TakePicture(OutputFileOptions output, kiln::Function<void(ImageCaptureResult)> done, int displayRotation) {
        Impl& m = *m_Impl;
        if (!m.options.imageCapture || !m.open || m.outputs.jpegSize.width <= 0) {
            if (done) done({ .error = ImageCaptureResult::Error::InvalidCamera, .message = "Not bound to a valid Camera" });
            return;
        }
        m.pictures.push_back({ std::move(output), std::move(done), displayRotation });
        RunNextPicture(m_Impl);
    }

    void Camera::SetPreviewSurface(ui::PlatformSurface surface, ui::IntSize size) {
        Impl& m = *m_Impl;
        if (m.outputs.preview == surface && m.outputs.previewSize == size) return;
        m.outputs.preview = surface;
        m.outputs.previewSize = surface.nativeWindow ? size : ui::IntSize{};
        m.Configure();
    }
    ui::IntSize Camera::PreviewSize() const {
        // Preview's default: 4:3 within 1080p.
        return ChoosePreviewSize(m_Impl->info.previewSizes, 4.0f / 3.0f);
    }

}
