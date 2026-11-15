// Camera: CameraX's ZoomMath, FocusMeteringControl's regions and results, the controls' rules,
// and ImageCapture's pipeline (CapturePipeline's 3A, the queue, saving), through a fake camera2.
#include "Check.h"

#include "kiln/media/Camera.h"
#include "kiln/runtime/Task.h"

#include <cstdio>
#include <deque>
#include <fstream>

using namespace kiln;
using namespace kiln::media;

namespace {
    // camera2 behind CameraHost: one camera; tests report frames and finish captures.
    struct FakeCamera {
        CameraInfo info;
        CameraHost::Callbacks callbacks;
        int opens = 0, configures = 0, closes = 0;
        CameraOutputs outputs;
        CameraRequest request;
        std::vector<CameraTrigger> triggers;
        struct Pending {
            CameraRequest request;
            int quality = 0, orientation = 0;
            kiln::Function<void(std::vector<std::uint8_t>, std::string)> done;
        };
        std::deque<Pending> captures;

        FakeCamera() {
            info.id = "0";
            info.facing = LensFacing::Back;
            info.sensorOrientation = 90;
            info.previewSizes = { { 1920, 1080 }, { 1440, 1080 }, { 640, 480 } };
            info.jpegSizes = { { 4000, 3000 }, { 4000, 2250 }, { 1600, 1200 } };
            info.hasFlashUnit = true;
            info.minZoomRatio = 0.6f;
            info.maxZoomRatio = 8;
            info.zoomRatioControl = true;
            info.activeArray = { 0, 0, 800, 600 };
            info.exposureCompensationMin = -12;
            info.exposureCompensationMax = 12;
            info.exposureCompensationStepNumerator = 1;
            info.exposureCompensationStepDenominator = 6;
            info.maxAfRegions = 3;
            info.maxAeRegions = 3;
            info.maxAwbRegions = 1;
            info.afModes = { 0, 1, 2, 3, 4 };
            info.aeModes = { 0, 1, 2, 3 };
            info.aeLockAvailable = info.awbLockAvailable = true;
        }
        void Install() {
            SetCameraHost({
                .cameras = [this] { return std::vector<CameraInfo>{ info }; },
                .open = [this](const std::string&, CameraHost::Callbacks c) {
                    ++opens;
                    callbacks = std::move(c);
                    return std::uint64_t(7);
                },
                .configure = [this](std::uint64_t, const CameraOutputs& o, const CameraRequest& r) {
                    ++configures;
                    outputs = o;
                    request = r;
                    return true;
                },
                .setRequest = [this](std::uint64_t, const CameraRequest& r) { request = r; },
                .trigger = [this](std::uint64_t, const CameraRequest&, CameraTrigger t) { triggers.push_back(t); },
                .capture = [this](std::uint64_t, const CameraRequest& r, int q, int o, auto done) {
                    captures.push_back({ r, q, o, std::move(done) });
                },
                .close = [this](std::uint64_t) { ++closes; },
            });
        }
        void Frame(int af = kAfStatePassiveFocused, int ae = kAeStateConverged, int awb = kAwbStateConverged) {
            callbacks.onResult(CameraFrameResult{ af, ae, awb });
        }
        ~FakeCamera() { SetCameraHost({}); }
    };

    struct Outcome {
        bool called = false;
        std::optional<CameraError> error;
        CameraDone Done() {
            called = false;
            error.reset();
            return [this](std::optional<CameraError> e) {
                called = true;
                error = std::move(e);
            };
        }
        bool Ok() const { return called && !error; }
        bool Failed(CameraError::Kind k) const { return called && error && error->kind == k; }
    };
}

TEST(CameraZoomMathAsCameraX) {
    // ZoomMathTest.
    CHECK(LinearZoomFromZoomRatio(0.6f, 0.6f, 8) == 0);
    CHECK(LinearZoomFromZoomRatio(8, 0.6f, 8) == 1);
    CHECK(LinearZoomFromZoomRatio(1, 1, 1) == 0);
    CHECK(ZoomRatioFromLinearZoom(0, 0.6f, 8) == 0.6f);
    CHECK(ZoomRatioFromLinearZoom(1, 0.6f, 8) == 8);
    CHECK(ZoomRatioFromLinearZoom(0, 1, 1) == 1 && ZoomRatioFromLinearZoom(0.5f, 1, 1) == 1 && ZoomRatioFromLinearZoom(1, 1, 1) == 1);
    const float linear = LinearZoomFromZoomRatio(2, 0.6f, 8);
    CHECK(std::abs(ZoomRatioFromLinearZoom(linear, 0.6f, 8) - 2) < 1e-4f);
    // Linear in the crop width: half way from 1x to 2x (of 1..2) is 4/3x.
    CHECK(std::abs(ZoomRatioFromLinearZoom(0.5f, 1, 2) - 4.0f / 3.0f) < 1e-5f);
}

TEST(CameraMeteringRegionsAsCameraX) {
    auto one = [](float x, float y, ui::IntRect crop, float aspect) {
        auto r = MeteringRegionsFromMeteringPoints({ MeteringPoint{ x, y } }, 1, crop, aspect);
        return r.size() == 1 ? r[0].rect : ui::IntRect{ -1, -1, -1, -1 };
    };
    // The field of view's aspect ratio as the crop region's: the 0.15 square, clipped to it.
    CHECK((one(0, 0, { 0, 0, 800, 600 }, 4.0f / 3) == ui::IntRect{ 0, 0, 60, 45 }));
    CHECK((one(0.5f, 0.5f, { 0, 0, 800, 600 }, 4.0f / 3) == ui::IntRect{ 340, 255, 460, 345 }));
    CHECK((one(1, 1, { 0, 0, 800, 600 }, 4.0f / 3) == ui::IntRect{ 740, 555, 800, 600 }));
    // Wider than the crop region: letterboxed vertically.
    CHECK((one(0, 0, { 0, 0, 400, 400 }, 4.0f / 3) == ui::IntRect{ 0, 20, 30, 80 }));
    CHECK((one(0.5f, 0.5f, { 0, 0, 400, 400 }, 4.0f / 3) == ui::IntRect{ 170, 170, 230, 230 }));
    CHECK((one(1, 1, { 0, 0, 400, 400 }, 4.0f / 3) == ui::IntRect{ 370, 320, 400, 380 }));
    // Narrower: pillarboxed.
    CHECK((one(0, 0, { 0, 0, 400, 400 }, 3.0f / 4) == ui::IntRect{ 20, 0, 80, 30 }));
    CHECK((one(1, 1, { 0, 0, 400, 400 }, 3.0f / 4) == ui::IntRect{ 320, 370, 380, 400 }));
    // Invalid points are skipped; at most maxRegions; none without regions.
    CHECK(MeteringRegionsFromMeteringPoints({ { 1.1f, 0.5f }, { 0.5f, -0.1f } }, 3, { 0, 0, 800, 600 }, 4.0f / 3).empty());
    CHECK(MeteringRegionsFromMeteringPoints({ { 0.1f, 0.1f }, { 0.2f, 0.2f }, { 0.3f, 0.3f } }, 2, { 0, 0, 800, 600 }, 4.0f / 3).size() == 2);
    CHECK(MeteringRegionsFromMeteringPoints({ { 0.5f, 0.5f } }, 0, { 0, 0, 800, 600 }, 4.0f / 3).empty());
    // A tap on the preview maps back through its turn: a back camera at 90 degrees on a portrait
    // display; the top-left of the element is the sensor frame's bottom-left.
    const MeteringPoint p = PreviewMeteringPoint({ 0, 0 }, { 300, 400 }, 90, false, { 1440, 1080 });
    CHECK(std::abs(p.x - 0) < 1e-4f && std::abs(p.y - 1) < 1e-4f && p.surfaceAspectRatio && std::abs(*p.surfaceAspectRatio - 4.0f / 3) < 1e-5f);
    const MeteringPoint c = PreviewMeteringPoint({ 150, 200 }, { 300, 400 }, 90, false, { 1440, 1080 });
    CHECK(std::abs(c.x - 0.5f) < 1e-4f && std::abs(c.y - 0.5f) < 1e-4f);
}

TEST(CameraControlsAsCameraX) {
    auto& d = rt::Dispatcher::Main();
    const bool wasVirtual = d.VirtualTime();
    d.SetVirtualTime(true);
    FakeCamera fake;
    fake.Install();
    {
        Camera camera("0", { .imageCapture = ImageCaptureOptions{} });
        CHECK(fake.opens == 1 && camera.State().type == CameraState::Type::Opening);
        CHECK(camera.State().exposure.isExposureCompensationSupported && camera.State().exposure.rangeLower == -12);
        Outcome o;
        // Not open yet: the zoom state changes, the operation is cancelled.
        camera.SetZoomRatio(2, o.Done());
        CHECK(o.Failed(CameraError::Kind::OperationCanceled) && o.error->message == "Camera is not active." && camera.State().zoom.zoomRatio == 2);
        camera.EnableTorch(true, o.Done());
        CHECK(o.Failed(CameraError::Kind::OperationCanceled) && camera.State().torch == TorchState::Off);
        fake.callbacks.onOpened();
        // Opened: configured with the 4:3 JPEG stream, the zoom asked for before.
        CHECK(camera.State().type == CameraState::Type::Open && fake.configures == 1);
        CHECK((fake.outputs.jpegSize == ui::IntSize{ 4000, 3000 }) && fake.request.zoomRatio == 2 && fake.request.afMode == 4);
        // Zoom: out of range fails; in range completes with the next frame; a newer one cancels it.
        camera.SetZoomRatio(9, o.Done());
        CHECK(o.Failed(CameraError::Kind::IllegalArgument));
        Outcome first, second;
        camera.SetZoomRatio(3, first.Done());
        camera.SetLinearZoom(1, second.Done());
        CHECK(first.Failed(CameraError::Kind::OperationCanceled) && !second.called);
        CHECK(camera.State().zoom.zoomRatio == 8 && camera.State().zoom.linearZoom == 1 && fake.request.zoomRatio == 8);
        fake.Frame();
        CHECK(second.Ok());
        camera.SetLinearZoom(1.5f, o.Done());
        CHECK(o.Failed(CameraError::Kind::IllegalArgument));
        // Torch: on at once, AE kept on, done with the frame.
        camera.EnableTorch(true, o.Done());
        CHECK(!o.called && camera.State().torch == TorchState::On && fake.request.torch && fake.request.aeMode == 1);
        fake.Frame();
        CHECK(o.Ok());
        camera.EnableTorch(false);
        // Exposure compensation: within the range.
        camera.SetExposureCompensationIndex(13, o.Done());
        CHECK(o.Failed(CameraError::Kind::IllegalArgument));
        camera.SetExposureCompensationIndex(-3, o.Done());
        fake.Frame();
        CHECK(o.Ok() && fake.request.aeCompensation == -3 && camera.State().exposure.exposureCompensationIndex == -3);
        // Flash modes choose AE's mode.
        camera.SetFlashMode(FlashMode::Auto);
        CHECK(fake.request.aeMode == 2);
        camera.SetFlashMode(FlashMode::On);
        CHECK(fake.request.aeMode == 3);
        camera.SetFlashMode(FlashMode::Off);
        CHECK(fake.request.aeMode == 1);
        // The preview's surface reconfigures the session.
        int window = 0;
        camera.SetPreviewSurface({ &window }, camera.PreviewSize());
        CHECK(fake.configures == 2 && (fake.outputs.previewSize == ui::IntSize{ 1440, 1080 }));
        // A fatal error closes it; controls are then not active.
        fake.callbacks.onError(CameraState::Error::CameraInUse, "taken");
        CHECK(camera.State().type == CameraState::Type::Closed && camera.State().error == CameraState::Error::CameraInUse);
        camera.SetZoomRatio(1, o.Done());
        CHECK(o.Failed(CameraError::Kind::OperationCanceled));
    }
    CHECK(fake.closes == 1);
    {
        // No flash unit: the torch is refused.
        fake.info.hasFlashUnit = false;
        Camera camera("0");
        fake.callbacks.onOpened();
        Outcome o;
        camera.EnableTorch(true, o.Done());
        CHECK(o.Failed(CameraError::Kind::IllegalState) && o.error->message == "No flash unit");
        fake.info.hasFlashUnit = true;
    }
    d.RunPending();
    d.SetVirtualTime(wasVirtual);
}

TEST(CameraFocusAndMeteringAsCameraX) {
    auto& d = rt::Dispatcher::Main();
    const bool wasVirtual = d.VirtualTime();
    d.SetVirtualTime(true);
    FakeCamera fake;
    fake.Install();
    {
        Camera camera("0");
        std::optional<FocusMeteringResult> result;
        std::optional<CameraError> error;
        int calls = 0;
        auto done = [&](std::optional<FocusMeteringResult> r, std::optional<CameraError> e) {
            ++calls;
            result = r;
            error = e;
        };
        camera.StartFocusAndMetering(FocusMeteringAction::Of({ 0.5f, 0.5f }), done);
        CHECK(calls == 1 && error && error->message == "Camera is not active.");
        fake.callbacks.onOpened();
        // A tap: AF locks (AUTO mode, triggered), AE and AWB only meter; the regions in the
        // active array (800x600, the preview's 4:3).
        calls = 0;
        camera.StartFocusAndMetering(FocusMeteringAction::Of({ 0.5f, 0.5f }), done);
        CHECK(fake.request.afMode == 1 && fake.triggers.back() == CameraTrigger::AfStart);
        CHECK(fake.request.afRegions && fake.request.afRegions->size() == 1 && ((*fake.request.afRegions)[0].rect == ui::IntRect{ 340, 255, 460, 345 }));
        CHECK(fake.request.aeRegions && fake.request.awbRegions && !fake.request.aeLock);
        fake.Frame(kAfStateActiveScan);
        CHECK(calls == 0);
        fake.Frame(kAfStateFocusedLocked);
        CHECK(calls == 1 && result && result->isFocusSuccessful && !error);
        // Not in focus when locked.
        calls = 0;
        camera.StartFocusAndMetering(FocusMeteringAction::Of({ 0.2f, 0.2f }), done);
        fake.Frame(kAfStateNotFocusedLocked);
        CHECK(calls == 1 && result && !result->isFocusSuccessful);
        // A newer action cancels the one in progress.
        int firstCalls = 0;
        std::optional<CameraError> firstError;
        camera.StartFocusAndMetering(FocusMeteringAction::Of({ 0.5f, 0.5f }), [&](auto, std::optional<CameraError> e) {
            ++firstCalls;
            firstError = e;
        });
        calls = 0;
        camera.StartFocusAndMetering(FocusMeteringAction::Of({ 0.6f, 0.6f }), done);
        CHECK(firstCalls == 1 && firstError && firstError->kind == CameraError::Kind::OperationCanceled);
        // Not focused within 5 s: unsuccessful; auto cancel (also 5 s) resets AF afterwards.
        d.AdvanceTimeBy(std::chrono::milliseconds(5000));
        d.RunPending();
        CHECK(calls == 1 && result && !result->isFocusSuccessful);
        CHECK(fake.request.afMode == 4 && fake.triggers.back() == CameraTrigger::AfCancel);
        CHECK(fake.request.afRegions && (*fake.request.afRegions)[0].weight == 0);
        // A shorter auto cancel cancels before the focus timeout: the operation is cancelled.
        calls = 0;
        auto shortAction = FocusMeteringAction::Of({ 0.5f, 0.5f });
        shortAction.autoCancelDurationMs = 500;
        camera.StartFocusAndMetering(shortAction, done);
        d.AdvanceTimeBy(std::chrono::milliseconds(500));
        d.RunPending();
        CHECK(calls == 1 && (!result || !result->isFocusSuccessful));
        // AE locking too: AE converges and locks, then AF.
        calls = 0;
        auto lockAe = FocusMeteringAction::Of({ 0.5f, 0.5f });
        lockAe.lockingMode = kMeteringAf | kMeteringAe;
        camera.StartFocusAndMetering(lockAe, done);
        fake.Frame(kAfStateActiveScan, kAeStateSearching);
        CHECK(!fake.request.aeLock && calls == 0);
        fake.Frame(kAfStateActiveScan, kAeStateConverged);
        CHECK(fake.request.aeLock && calls == 0);
        fake.Frame(kAfStateFocusedLocked, kAeStateLocked);
        CHECK(calls == 1 && result && result->isFocusSuccessful);
        camera.CancelFocusAndMetering();
        CHECK(!fake.request.aeLock && fake.request.afMode == 4);
        // AE / AWB points only: regions update, no AF; the next frame answers, not focused.
        calls = 0;
        camera.StartFocusAndMetering(FocusMeteringAction::Of({ 0.5f, 0.5f }, kMeteringAe | kMeteringAwb), done);
        CHECK(fake.request.afMode == 4);
        fake.Frame();
        CHECK(calls == 1 && result && !result->isFocusSuccessful);
        // Points the camera cannot meter: refused.
        fake.info.maxAfRegions = fake.info.maxAeRegions = fake.info.maxAwbRegions = 0;
    }
    {
        Camera camera("0");
        fake.callbacks.onOpened();
        std::optional<CameraError> error;
        camera.StartFocusAndMetering(FocusMeteringAction::Of({ 0.5f, 0.5f }), [&](auto, std::optional<CameraError> e) { error = e; });
        CHECK(error && error->kind == CameraError::Kind::IllegalArgument);
        CHECK(!camera.IsFocusMeteringSupported(FocusMeteringAction::Of({ 0.5f, 0.5f })));
    }
    d.RunPending();
    d.SetVirtualTime(wasVirtual);
}

TEST(CameraTakesPicturesAsCameraX) {
    auto& d = rt::Dispatcher::Main();
    const bool wasVirtual = d.VirtualTime();
    d.SetVirtualTime(true);
    FakeCamera fake;
    fake.Install();
    {
        // Without ImageCapture bound: an invalid camera.
        Camera preview("0");
        fake.callbacks.onOpened();
        ImageCaptureResult r;
        preview.TakePicture({}, [&](ImageCaptureResult x) { r = std::move(x); });
        CHECK(r.error == ImageCaptureResult::Error::InvalidCamera);
    }
    {
        Camera camera("0", { .imageCapture = ImageCaptureOptions{} });
        fake.callbacks.onOpened();
        // Minimizing latency, flash off: captured at once, quality 95, upright for the display.
        std::vector<ImageCaptureResult> results;
        camera.TakePicture({}, [&](ImageCaptureResult x) { results.push_back(std::move(x)); }, 90);
        CHECK(fake.captures.size() == 1 && fake.triggers.empty() && fake.captures[0].quality == 95 && fake.captures[0].orientation == 0);
        // Pictures go one at a time.
        const std::string file = "/tmp/kiln-camera-test.jpg";
        camera.TakePicture({ .file = file }, [&](ImageCaptureResult x) { results.push_back(std::move(x)); });
        CHECK(fake.captures.size() == 1);
        auto first = std::move(fake.captures.front());
        fake.captures.pop_front();
        first.done({ 0xFF, 0xD8, 0xFF }, {});
        CHECK(results.size() == 1 && results[0].error == ImageCaptureResult::Error::None && results[0].jpeg.size() == 3);
        CHECK(fake.captures.size() == 1 && fake.captures[0].orientation == 90);
        auto second = std::move(fake.captures.front());
        fake.captures.pop_front();
        second.done({ 1, 2, 3, 4 }, {});
        CHECK(results.size() == 2 && results[1].savedUri == file);
        std::ifstream in(file, std::ios::binary);
        std::string bytes((std::istreambuf_iterator<char>(in)), {});
        CHECK(bytes.size() == 4);
        std::remove(file.c_str());
        // A failed capture; an unwritable file.
        camera.TakePicture({}, [&](ImageCaptureResult x) { results.push_back(std::move(x)); });
        std::exchange(fake.captures.front().done, {})({}, "boom");
        fake.captures.pop_front();
        CHECK(results.back().error == ImageCaptureResult::Error::CaptureFailed && results.back().message == "boom");
        camera.TakePicture({ .file = "/nonexistent-dir/x.jpg" }, [&](ImageCaptureResult x) { results.push_back(std::move(x)); });
        std::exchange(fake.captures.front().done, {})({ 1 }, {});
        fake.captures.pop_front();
        CHECK(results.back().error == ImageCaptureResult::Error::FileIo);

        // Flash auto with AE asking for it: AE precapture, wait for AE, capture, cancel precapture.
        camera.SetFlashMode(FlashMode::Auto);
        fake.Frame(kAfStatePassiveFocused, kAeStateFlashRequired);
        camera.TakePicture({}, [&](ImageCaptureResult x) { results.push_back(std::move(x)); });
        CHECK(fake.triggers.back() == CameraTrigger::AePrecaptureStart && fake.captures.empty());
        fake.Frame(kAfStatePassiveFocused, kAeStatePrecapture);
        CHECK(fake.captures.empty());
        fake.Frame(kAfStatePassiveFocused, kAeStateConverged);
        CHECK(fake.captures.size() == 1 && fake.captures[0].request.aeMode == 2);
        std::exchange(fake.captures.front().done, {})({ 9 }, {});
        fake.captures.pop_front();
        CHECK(fake.triggers.back() == CameraTrigger::AePrecaptureCancel);
        // AE never settles: captured after the 5 s flash limit anyway.
        camera.SetFlashMode(FlashMode::On);
        camera.TakePicture({}, [&](ImageCaptureResult x) { results.push_back(std::move(x)); });
        fake.Frame(kAfStatePassiveFocused, kAeStatePrecapture);
        CHECK(fake.captures.empty());
        d.AdvanceTimeBy(std::chrono::milliseconds(5000));
        d.RunPending();
        CHECK(fake.captures.size() == 1);
        // The camera closes with a picture pending: it fails as closed.
        fake.callbacks.onError(CameraState::Error::CameraFatal, "gone");
        CHECK(results.back().error == ImageCaptureResult::Error::CameraClosed);
    }
    {
        // Maximizing quality: AF triggered and AWB awaited too; quality 100.
        fake.captures.clear();
        Camera camera("0", { .imageCapture = ImageCaptureOptions{ .captureMode = CaptureMode::MaximizeQuality } });
        fake.callbacks.onOpened();
        fake.triggers.clear();
        camera.TakePicture({}, [](ImageCaptureResult) {});
        CHECK((fake.triggers == std::vector<CameraTrigger>{ CameraTrigger::AePrecaptureStart, CameraTrigger::AfStart }));
        fake.Frame(kAfStateFocusedLocked, kAeStateConverged, kAwbStateSearching);
        CHECK(fake.captures.empty());
        fake.Frame(kAfStateFocusedLocked, kAeStateConverged, kAwbStateConverged);
        CHECK(fake.captures.size() == 1 && fake.captures[0].quality == 100);
        std::exchange(fake.captures.front().done, {})({ 1 }, {});
        fake.captures.pop_front();
        CHECK((std::vector<CameraTrigger>(fake.triggers.end() - 2, fake.triggers.end()) ==
               std::vector<CameraTrigger>{ CameraTrigger::AePrecaptureCancel, CameraTrigger::AfCancel }));
    }
    d.RunPending();
    d.SetVirtualTime(wasVirtual);
}
