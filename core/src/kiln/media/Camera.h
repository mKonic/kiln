#pragma once

// The device's cameras, as CameraX gives them: a Camera opened with its use cases (a preview, still
// capture), its state (zoom, exposure, torch, open or not) and its controls (zoom, torch, exposure
// compensation, focus and metering), taking pictures (ImageCapture) through camera2's 3A
// sequences as CameraX's CapturePipeline runs them. Android: the NDK's camera2 (ACameraManager).
// CameraPreview (kiln/foundation/VideoSurface.h) shows a camera; the app needs
// android.permission.CAMERA granted first (contracts::RequestPermission).
//
//   media::Camera camera(back->id, { .imageCapture = media::ImageCaptureOptions{} });
//   CameraPreview(FillMax(Modifier{}, Axis::Both), camera);
//   camera.SetZoomRatio(2);
//   camera.TakePicture({ .file = dir + "/photo.jpg" }, [](media::ImageCaptureResult r) { ... });

#include "kiln/runtime/Flow.h"
#include "kiln/ui/ExternalSurface.h"
#include "kiln/ui/Geometry.h"
#include <kiln/Function.h>

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace kiln::media {

    // CameraCharacteristics.LENS_FACING.
    enum class LensFacing { Front, Back, External };

    // The camera's characteristics kiln uses (CameraInfo, Camera2CameraInfo).
    struct CameraInfo {
        std::string id;
        LensFacing facing = LensFacing::Back;
        // Clockwise degrees the sensor's image turns to stand upright on the device in its
        // natural orientation (SENSOR_ORIENTATION).
        int sensorOrientation = 0;
        // The sizes a preview stream (a surface), a JPEG and a YUV stream can have, in the
        // sensor's orientation.
        std::vector<ui::IntSize> previewSizes, jpegSizes, yuvSizes;
        bool hasFlashUnit = false;                 // FLASH_INFO_AVAILABLE
        // CONTROL_ZOOM_RATIO_RANGE (Android 11+: zoomRatioControl), else 1 to
        // SCALER_AVAILABLE_MAX_DIGITAL_ZOOM through the crop region.
        float minZoomRatio = 1, maxZoomRatio = 1;
        bool zoomRatioControl = false;
        ui::IntRect activeArray;                   // SENSOR_INFO_ACTIVE_ARRAY_SIZE
        // CONTROL_AE_COMPENSATION_RANGE and _STEP (a rational, in EV).
        int exposureCompensationMin = 0, exposureCompensationMax = 0;
        int exposureCompensationStepNumerator = 0, exposureCompensationStepDenominator = 1;
        int maxAfRegions = 0, maxAeRegions = 0, maxAwbRegions = 0;   // CONTROL_MAX_REGIONS
        std::vector<int> afModes, aeModes;          // CONTROL_AF / AE_AVAILABLE_MODES
        bool aeLockAvailable = false, awbLockAvailable = false;
        bool operator==(const CameraInfo&) const = default;
    };

    std::vector<CameraInfo> Cameras();
    std::optional<CameraInfo> FindCamera(LensFacing facing);

    // The preview size for a surface: the largest the camera offers within 1920 x 1080 (the
    // preview limit CameraX keeps), with the aspect ratio nearest `aspect` (width / height in the
    // sensor's orientation; 0: any).
    ui::IntSize ChoosePreviewSize(const std::vector<ui::IntSize>& sizes, float aspect = 0);
    // A still capture's size: the largest the camera offers with the aspect ratio nearest
    // `aspect` (0: the largest).
    ui::IntSize ChooseCaptureSize(const std::vector<ui::IntSize>& sizes, float aspect = 0);

    // How far the preview turns to stand upright on the display (CameraX's relative rotation:
    // the sensor's orientation less the display's rotation, plus it for a front camera).
    int PreviewRotation(const CameraInfo& camera, int displayRotation);
    // The matrix (row-major 3x3, element pixels) that turns a sensor-oriented frame, drawn
    // stretched over a w x h element, upright and unstretched (CameraX PreviewTransformation's
    // TextureView correction); `mirror` flips it as a front camera's preview is shown.
    std::array<float, 9> PreviewTransform(int rotationDegrees, bool mirror, float width, float height);

    // --- ZoomMath, metering --------------------------------------------------------------------

    // ZoomMath: linear zoom is linear in the cropped width (0: the widest, 1: the narrowest).
    float LinearZoomFromZoomRatio(float zoomRatio, float minZoomRatio, float maxZoomRatio);
    float ZoomRatioFromLinearZoom(float linearZoom, float minZoomRatio, float maxZoomRatio);

    // A point to focus or meter on, normalized (0..1) in the sensor-oriented frame; its size is
    // a fraction of the frame (MeteringPointFactory.getDefaultPointSize: 0.15). The aspect ratio
    // of the surface it was tapped on (its field of view) corrects it to the sensor's.
    struct MeteringPoint {
        float x = 0.5f, y = 0.5f, size = 0.15f;
        std::optional<float> surfaceAspectRatio;
        bool operator==(const MeteringPoint&) const = default;
    };
    // The metering point for a tap on CameraPreview's element (PreviewView.getMeteringPointFactory):
    // through the preview's transform back to the camera's frame.
    MeteringPoint PreviewMeteringPoint(ui::Offset tap, ui::IntSize element, int rotationDegrees, bool mirror, ui::IntSize previewSize);

    // FocusMeteringAction.FLAG_*.
    inline constexpr int kMeteringAf = 1, kMeteringAe = 2, kMeteringAwb = 4;
    struct FocusMeteringAction {
        std::vector<MeteringPoint> afPoints, aePoints, awbPoints;
        // Cancelled (focus and metering back to continuous) after this long; 0: never.
        std::int64_t autoCancelDurationMs = 5000;
        int lockingMode = kMeteringAf;      // DEFAULT_LOCKING_MODE: AF locks, AE / AWB only meter
        // Builder(point, meteringMode): AF, AE and AWB by default.
        static FocusMeteringAction Of(MeteringPoint point, int modes = kMeteringAf | kMeteringAe | kMeteringAwb);
        FocusMeteringAction& AddPoint(MeteringPoint point, int modes = kMeteringAf | kMeteringAe | kMeteringAwb);
    };
    struct FocusMeteringResult {
        bool isFocusSuccessful = false;
    };
    // MeteringRectangle in sensor coordinates (METERING_WEIGHT_MAX: 1000).
    struct MeteringRectangle {
        ui::IntRect rect;
        int weight = 1000;
        bool operator==(const MeteringRectangle&) const = default;
    };
    // FocusMeteringControl.meteringRegionsFromMeteringPoints: up to maxRegions valid points, as
    // rectangles in the crop region, corrected for the field of view's aspect ratio.
    std::vector<MeteringRectangle> MeteringRegionsFromMeteringPoints(const std::vector<MeteringPoint>& points, int maxRegions,
                                                                     ui::IntRect cropRegion, float defaultAspectRatio);

    // --- state ----------------------------------------------------------------------------------

    struct ZoomState {
        float zoomRatio = 1, minZoomRatio = 1, maxZoomRatio = 1, linearZoom = 0;
        bool operator==(const ZoomState&) const = default;
    };
    struct ExposureState {
        int exposureCompensationIndex = 0;
        int rangeLower = 0, rangeUpper = 0;
        int stepNumerator = 0, stepDenominator = 1;   // EV per index
        bool isExposureCompensationSupported = false;
        bool operator==(const ExposureState&) const = default;
    };
    enum class TorchState { Off = 0, On = 1 };
    // CameraState: the camera's lifecycle (opening as soon as it is made) and why it failed.
    struct CameraState {
        enum class Type { PendingOpen, Opening, Open, Closing, Closed };
        // CameraState.ERROR_*.
        enum class Error { None = 0, MaxCamerasInUse = 1, CameraInUse = 2, OtherRecoverable = 3, StreamConfig = 4,
                           CameraDisabled = 5, CameraFatal = 6, DoNotDisturbModeEnabled = 7 };
        Type type = Type::PendingOpen;
        Error error = Error::None;
        std::string errorMessage;
        ZoomState zoom;
        ExposureState exposure;
        TorchState torch = TorchState::Off;
        bool operator==(const CameraState&) const = default;
    };

    // Why a control's operation failed (the exception its ListenableFuture fails with).
    struct CameraError {
        enum class Kind { IllegalArgument, IllegalState, OperationCanceled };
        Kind kind = Kind::OperationCanceled;
        std::string message;
        bool operator==(const CameraError&) const = default;
    };
    using CameraDone = kiln::Function<void(std::optional<CameraError> error)>;

    // --- ImageCapture ---------------------------------------------------------------------------

    // ImageCapture.CAPTURE_MODE_* and FLASH_MODE_*.
    enum class CaptureMode { MaximizeQuality = 0, MinimizeLatency = 1 };
    enum class FlashMode { Auto = 0, On = 1, Off = 2 };
    struct ImageCaptureOptions {
        CaptureMode captureMode = CaptureMode::MinimizeLatency;
        FlashMode flashMode = FlashMode::Off;
        int jpegQuality = 0;                 // 1..100; 0: 100 maximizing quality, 95 minimizing latency
        float targetAspectRatio = 0;          // width / height in the sensor's orientation; 0: the largest size
    };
    // Where a picture goes (OutputFileOptions): a file, or, with `file` empty, the JPEG bytes
    // to the callback (OnImageCapturedCallback).
    struct OutputFileOptions {
        std::string file;
    };
    struct ImageCaptureResult {
        // ImageCaptureException's codes.
        enum class Error { None = -1, Unknown = 0, FileIo = 1, CaptureFailed = 2, CameraClosed = 3, InvalidCamera = 4 };
        Error error = Error::None;
        std::string message;
        std::string savedUri;                 // the file written (OutputFileResults.getSavedUri)
        std::vector<std::uint8_t> jpeg;        // without a file: the picture
        int rotationDegrees = 0;              // its EXIF orientation (ImageInfo.getRotationDegrees)
    };

    // --- the platform (camera2) -----------------------------------------------------------------

    // CaptureRequest keys kiln sets (camera2 values: CONTROL_AF_MODE 1 auto, 4 continuous
    // picture; CONTROL_AE_MODE 1 on, 2 on auto flash, 3 on always flash).
    struct CameraRequest {
        float zoomRatio = 1;                  // CONTROL_ZOOM_RATIO, with zoomRatioControl
        std::optional<ui::IntRect> cropRegion;   // SCALER_CROP_REGION, without it
        bool torch = false;                   // FLASH_MODE_TORCH
        int aeCompensation = 0;
        int afMode = 4, aeMode = 1;
        // Regions to set (an empty rectangle with weight 0 resets them); none: not set.
        std::optional<std::vector<MeteringRectangle>> afRegions, aeRegions, awbRegions;
        bool aeLock = false, awbLock = false;
        bool operator==(const CameraRequest&) const = default;
    };
    // CONTROL_AF_TRIGGER / CONTROL_AE_PRECAPTURE_TRIGGER for one request.
    enum class CameraTrigger { AfStart, AfCancel, AePrecaptureStart, AePrecaptureCancel };
    // A completed frame's 3A states (CaptureResult CONTROL_AF_STATE, _AE_STATE, _AWB_STATE).
    struct CameraFrameResult {
        int afState = 0, aeState = 0, awbState = 0;
    };
    // camera2's states kiln reads.
    inline constexpr int kAfStateInactive = 0, kAfStatePassiveScan = 1, kAfStatePassiveFocused = 2, kAfStateActiveScan = 3,
                         kAfStateFocusedLocked = 4, kAfStateNotFocusedLocked = 5, kAfStatePassiveUnfocused = 6;
    inline constexpr int kAeStateInactive = 0, kAeStateSearching = 1, kAeStateConverged = 2, kAeStateLocked = 3,
                         kAeStateFlashRequired = 4, kAeStatePrecapture = 5;
    inline constexpr int kAwbStateInactive = 0, kAwbStateSearching = 1, kAwbStateConverged = 2, kAwbStateLocked = 3;

    // The session's outputs: the preview surface (none: no preview yet), a JPEG stream for still
    // capture (an empty size: none).
    struct CameraOutputs {
        ui::PlatformSurface preview;
        ui::IntSize previewSize, jpegSize;
        bool operator==(const CameraOutputs&) const = default;
    };
    struct CameraHost {
        kiln::Function<std::vector<CameraInfo>()> cameras;
        // Opens the camera (async); onOpened, onError (CameraState::Error) and onResult (every
        // completed frame) run on the main thread. Returns a handle, 0 if it cannot start.
        struct Callbacks {
            kiln::Function<void()> onOpened;
            kiln::Function<void(CameraState::Error error, std::string message)> onError;
            kiln::Function<void(const CameraFrameResult& result)> onResult;
        };
        kiln::Function<std::uint64_t(const std::string& cameraId, Callbacks callbacks)> open;
        // (Re)configures the capture session with these outputs and starts the repeating request.
        kiln::Function<bool(std::uint64_t camera, const CameraOutputs& outputs, const CameraRequest& request)> configure;
        // Changes the repeating request.
        kiln::Function<void(std::uint64_t camera, const CameraRequest& request)> setRequest;
        // One request with the repeating one's settings and a trigger.
        kiln::Function<void(std::uint64_t camera, const CameraRequest& request, CameraTrigger trigger)> trigger;
        // A still capture (TEMPLATE_STILL_CAPTURE into the JPEG stream, JPEG_QUALITY,
        // JPEG_ORIENTATION); done runs on the main thread with the JPEG, or empty on failure.
        kiln::Function<void(std::uint64_t camera, const CameraRequest& request, int jpegQuality, int orientation,
                            kiln::Function<void(std::vector<std::uint8_t> jpeg, std::string error)> done)> capture;
        // Closes it; its surfaces are free when this returns.
        kiln::Function<void(std::uint64_t camera)> close;
    };
    void SetCameraHost(CameraHost host);
    const CameraHost& GetCameraHost();

    // --- Camera ---------------------------------------------------------------------------------

    struct CameraOptions {
        // ImageCapture bound with the preview; none: preview only.
        std::optional<ImageCaptureOptions> imageCapture;
    };

    // A camera with its use cases (ProcessCameraProvider.bindToLifecycle's Camera): it opens when
    // made and closes when destroyed. Its controls (CameraControl) answer through `done`, on the
    // main thread; a control on a camera that is not open fails with OperationCanceled.
    class Camera {
    public:
        explicit Camera(std::string cameraId, CameraOptions options = {});
        ~Camera();
        Camera(const Camera&) = delete;
        Camera& operator=(const Camera&) = delete;

        const CameraInfo& Info() const;
        const CameraState& State() const;
        rt::MutableStateFlow<CameraState>& StateFlow();

        // CameraControl.
        void SetZoomRatio(float ratio, CameraDone done = {});
        void SetLinearZoom(float linearZoom, CameraDone done = {});
        void EnableTorch(bool on, CameraDone done = {});
        void SetExposureCompensationIndex(int index, CameraDone done = {});
        void StartFocusAndMetering(FocusMeteringAction action,
                                   kiln::Function<void(std::optional<FocusMeteringResult>, std::optional<CameraError>)> done = {});
        void CancelFocusAndMetering(CameraDone done = {});
        bool IsFocusMeteringSupported(const FocusMeteringAction& action) const;

        // ImageCapture.
        void SetFlashMode(FlashMode mode);
        FlashMode GetFlashMode() const;
        // Takes a picture, upright for this display rotation (targetRotation; Surface.ROTATION_*
        // in degrees). done runs once, on the main thread.
        void TakePicture(OutputFileOptions output, kiln::Function<void(ImageCaptureResult)> done, int displayRotation = 0);

        // The preview's surface (CameraPreview sets it); an empty surface stops the preview.
        void SetPreviewSurface(ui::PlatformSurface surface, ui::IntSize size);
        ui::IntSize PreviewSize() const;   // the size CameraPreview asks for

        struct Impl;

    private:
        std::shared_ptr<Impl> m_Impl;
    };

}
